#include <iostream>
#include <fstream>
#include <pthread.h>
#include <vector>
#include <unistd.h>
#include <semaphore.h>
#include <queue>
#include <string>
#include <string.h>
#include <signal.h>
#include <numeric>

struct Exam {
    int student_id;
    std::string student_answers;
    int score;
    bool is_flagged;
    std::string sub_type;
};


int active_students = 0;
int early_count = 0, ontime_count = 0, timeout_count = 0, flagged_count = 0;
std::vector<int> final_scores;
std::queue<Exam> submission_queue;

pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
sem_t items_in_queue;
sem_t grading_slots;

thread_local int current_sid;
const std::string ANSWER_KEY = "ABCDE";

void update_dashboard(std::string status) {
    pthread_mutex_lock(&mtx);
    std::ofstream f("dashboard_data.json");
    double avg = final_scores.empty() ? 0 : accumulate(final_scores.begin(), final_scores.end(), 0.0) / final_scores.size();
    
    f << "{\n"
      << "  \"active\": " << (active_students < 0 ? 0 : active_students) << ",\n"
      << "  \"queued\": " << (int)submission_queue.size() << ",\n"
      << "  \"early\": " << early_count << ",\n"
      << "  \"ontime\": " << ontime_count << ",\n"
      << "  \"timeout\": " << timeout_count << ",\n"
      << "  \"flagged\": " << flagged_count << ",\n"
      << "  \"average\": " << avg << ",\n"
      << "  \"status\": \"" << status << "\"\n"
      << "}";
    f.close();
    pthread_mutex_unlock(&mtx);
}

void alarm_handler(int sig) {
    pthread_mutex_lock(&mtx);
    //timeout students only get 2 letters right
    submission_queue.push({current_sid, "AB...", 0, false, "Timed-Out"});
    timeout_count++;
    active_students--;
    pthread_mutex_unlock(&mtx);
    sem_post(&items_in_queue);
    update_dashboard("Student Timed Out");
    pthread_exit(NULL);
}

void* student_thread(void* arg) {
    //recieve id by casting
    current_sid = (intptr_t)arg; 
    
    signal(SIGALRM, alarm_handler);
    int work_time = rand() % 18 + 1; 
    alarm(12);

    pthread_mutex_lock(&mtx);
    active_students++;
    pthread_mutex_unlock(&mtx);
    update_dashboard("Exam in Progress");

    sleep(work_time);

    alarm(0); 
    pthread_mutex_lock(&mtx);
    bool cheating = (work_time < 4);
    if(cheating) flagged_count++;
    
    // random answers: ome get "ABCDE", some get "ABXXX"
    std::string ans = (rand() % 2 == 0) ? "ABCDE" : "ABCCD"; 
    
    if(work_time < 5) early_count++; else ontime_count++;
    submission_queue.push({current_sid, ans, 0, cheating, (work_time < 10 && work_time > 4 ? "Early" : "On-Time")});
    active_students--;
    pthread_mutex_unlock(&mtx);
    
    sem_post(&items_in_queue);
    update_dashboard("Submission Received");
    return NULL;
}

void* evaluator_thread(void* arg) {
    while(true) {
        sem_wait(&grading_slots);
        sem_wait(&items_in_queue);
        
        pthread_mutex_lock(&mtx);
        if(!submission_queue.empty()) {
            Exam e = submission_queue.front();
	        std::string subtype = e.sub_type;
            submission_queue.pop();
            int marks = 0;
            for(int i=0; i<5; i++) {
                if(i < e.student_answers.length() && e.student_answers[i] == ANSWER_KEY[i]) 
                    marks += 20;
            }
	    std::cout << "Grading student " << e.student_id << " (Status: " << subtype << ")" << " (Student's Answer: " << e.student_answers << ")" << " (Marks Obtained: " << marks <<" )\n";
	    if(e.is_flagged) { std::cout << "Student " << e.student_id << "may be suspected of cheating as their work_time is less than 4" << std::endl; } 
            final_scores.push_back(marks);
        }
        pthread_mutex_unlock(&mtx);
        
        sleep(1); 
        update_dashboard("Grading...");
        sem_post(&grading_slots);
    }
    return NULL;
}

int main() {
    srand(time(NULL));
    int n = 10;
    sem_init(&items_in_queue, 0, 0);
    sem_init(&grading_slots, 0, 0);

    pthread_t st[n], ev[3];

    for(int i=0; i<3; i++) pthread_create(&ev[i], NULL, evaluator_thread, NULL);
    
    for(int i=0; i<n; i++) {
       // pass ID as value, not pointer to local loop variable
        pthread_create(&st[i], NULL, student_thread, (void*)(intptr_t)(101 + i));
    }

    for(int i=0; i<n; i++) pthread_join(st[i], NULL);

    std::cout << "\n>>> PRESS ENTER TO START GRADING <<<\n";
    std::cin.get();

    for(int i=0; i<3; i++) sem_post(&grading_slots);

    while(true) {
        pthread_mutex_lock(&mtx);
        if(submission_queue.empty()) { pthread_mutex_unlock(&mtx); break; }
        pthread_mutex_unlock(&mtx);
        sleep(1);
    }

    update_dashboard("Grading Finished");
    return 0;
}