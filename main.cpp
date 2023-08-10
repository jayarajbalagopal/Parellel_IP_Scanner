#include <iostream>
#include <stdio.h>
#include <regex>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <fstream>
#include <string>
#include <vector>
#include <condition_variable>
#include <unistd.h>

#define BATCH_SIZE 3
#define NUM_THREADS 3

// Size of the buffer
const int NUM_BATCHES = 100;

using namespace std;

unordered_set<string> unique_v4_in_worker[NUM_THREADS];
unordered_set<string> unique_v6_in_worker[NUM_THREADS];
unordered_set<string> unique_v4;
unordered_set<string> unique_v6;

int total_v4_in_worker[NUM_THREADS];
int total_v6_in_worker[NUM_THREADS];
int invalid_in_worker[NUM_THREADS];

int total_v4 = 0;
int total_v6 = 0;
int invalid = 0;

vector<vector<string>> batches(NUM_BATCHES);

mutex slot_mutex;
vector<size_t> free_batch_slots;
vector<size_t> filled_batch_slots;

condition_variable cv_free;
condition_variable cv_fill;

bool all_batches_are_read = false;

class IPMatcher{
    const regex v4_regex = regex(
        "^(25[0-5]|2[0-4][0-9]|[1][0-9][0-9]|[0-9]|[1-9][0-9])\\."
        "(25[0-5]|2[0-4][0-9]|[1][0-9][0-9]|[0-9]|[1-9][0-9])\\."
        "(25[0-5]|2[0-4][0-9]|[1][0-9][0-9]|[0-9]|[1-9][0-9])\\."
        "(25[0-5]|2[0-4][0-9]|[1][0-9][0-9]|[0-9]|[1-9][0-9])$"
    );

    const regex v6_regex = regex("((([0-9a-fA-F]){1,4})\\:){7}([0-9a-fA-F]){1,4}");

public:
    IPMatcher(){}

    bool v4_match(string& IP) const{
        if(IP.size() > 15 || IP.size() < 7)
            return false;
        return regex_match(IP, v4_regex); 
    }

    bool v6_match(string& IP) const{
        if(IP.size() > 39)
            return false;
        return regex_match(IP, v6_regex);
    }
} matcher;

// Process the batch and record the IP information
void process_batch(vector<string>& batch, int thread_id){
    for(auto &item: batch){
        if(matcher.v4_match(item)){
            total_v4_in_worker[thread_id]++;
            unique_v4_in_worker[thread_id].insert(item);
        }
        else if(matcher.v6_match(item)){
            total_v6_in_worker[thread_id]++;
            unique_v6_in_worker[thread_id].insert(item);
        }
        else{
            invalid_in_worker[thread_id]++;
        }
    }
}

/* All workers maintain seperate counts,
this function combines the result of the each of the worker*/
void combine_results_from_workers() {
    for(int i = 0; i < NUM_THREADS; ++i) {
        total_v4 += total_v4_in_worker[i];
        total_v6 += total_v6_in_worker[i];
        invalid += invalid_in_worker[i];
        for(auto& ip: unique_v4_in_worker[i]) {
            unique_v4.insert(ip);
        }
        for(auto& ip: unique_v6_in_worker[i]) {
            unique_v6.insert(ip);
        }
    }
}

// CONSUMER
void thread_function(int thread_id){
    while (true) {
        vector<string> batch;
        // Wait for a batch to be available
        {
            unique_lock<mutex> lock(slot_mutex);
            // Stop waiting if all batches are read and there is no batch available
            // Or stop waiting if there is a batch available
            cv_fill.wait(lock, []() { return !filled_batch_slots.empty() || (all_batches_are_read && filled_batch_slots.empty()); });
        
            if(filled_batch_slots.empty()) {
                break;
            }
            size_t slot = filled_batch_slots.back();
            filled_batch_slots.pop_back();

            // Pick up the batch, and inform the producer
            batch.swap(batches[slot]);
            free_batch_slots.push_back(slot);
            lock.unlock();  
            cv_free.notify_one();
        }

        process_batch(batch, thread_id);
    }
}

int main(int argc, char* argv[]){
    auto usage = [argv](){ cout << "USAGE: " << argv[0] << " <Input file>" << endl; };
    if(argc != 2){
        usage();
        return 1;
    }
    string input_filename = argv[1];

    // Initialize free slots, initially all slots are free slots
    for(size_t i = 0; i < NUM_BATCHES; ++i) {
        free_batch_slots.push_back(i);
    }

    vector<thread> workers;
    for(int i=0;i<NUM_THREADS;i++){
        workers.push_back(thread(thread_function, i));
    }
    ifstream input(input_filename);
    string line;
     
    // PRODUCER
    while(!all_batches_are_read) {
        vector<string> current_batch;
    
        // Reading a batch
        string line;
        while(current_batch.size() < BATCH_SIZE and getline(input, line)){
            current_batch.push_back(line);
        }

        bool _all_batches_are_read = current_batch.size() < BATCH_SIZE;

        // Put the batch into a slot
        {
            unique_lock<mutex> lock(slot_mutex);
            // Wait until a free slot is available
            cv_free.wait(lock, []() { return !free_batch_slots.empty(); });
            size_t slot = free_batch_slots.back();
            free_batch_slots.pop_back();
            batches[slot].swap(current_batch);
            // Update the newly availble slot added by producer
            filled_batch_slots.push_back(slot);
            // Notify consumer that a slot is available to be consumer
            cv_fill.notify_all();
        }

        all_batches_are_read = _all_batches_are_read;
    }

    for(auto &t: workers){
        t.join();
    }

    combine_results_from_workers();
    input.close();

    cout << "TOTAL IPV4 " << total_v4 << endl;
    cout << "TOTAL_IPV6 " << total_v6 << endl;
    cout << "TOTAL_UNIQUE_IPV4 " << unique_v4.size() << endl;
    cout << "TOTAL_UNIQUE_IPV6 " << unique_v6.size() << endl;
    cout << "INVALID " << invalid << endl;
    return 0;
}