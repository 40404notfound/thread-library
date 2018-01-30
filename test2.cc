#include <iostream>
#include <fstream>
#include <list>
#include <vector>
#include <string>
#include <utility>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include "thread.h"
#include "disk.h"



using std::vector;
using std::list;
using std::pair;
using std::cin;
using std::cout;
using std::ifstream;
using std::make_pair;
using std::abs;
using std::min;

mutex mtx;
cv c_empty;
cv c_full;
list<pair<int32_t, unsigned int>> tasks;
vector<bool> file_valid;
vector<uint32_t> file_req_n;
vector<uint32_t> file_ser_n;
uint32_t max_disk_queue;
uint32_t max_file_n;
int32_t last_seek_pos = 0;
uint32_t g_argc;

/*
struct mtx_guard {
    mtx_guard(mutex& mtx_) : mtx{mtx_} {
        mtx.lock();
    }
    ~mtx_guard() {
        mtx.unlock();
    }
    mutex& mtx;
};
*/

struct req_t {
    unsigned int num;
    char* filename;
};

struct input_t {
    int argc;
    char** argv;
};

vector<req_t> vr;


/*
// Actually it's guarded in a mtx
void print_request(unsigned int requester, unsigned int track) {
    mtx_guard gd{mtx_io};
    cout << "requester " << requester << " track " << track << '\n';
}

void print_service(unsigned int requester, unsigned int track) {
    mtx_guard gd{mtx_io};
    cout << "service requester " << requester << " track " << track << '\n';
}
*/

void debug_task() {
    cout << tasks.size();
    for (auto& p : tasks) {
        cout << " (" << p.first << ", " << p.second << ") ";
    }
    cout << '\n';
}



void request(void* ptr) {
    mtx.lock();
    req_t* rt = reinterpret_cast<req_t*>(ptr);
    ifstream in{reinterpret_cast<const char*>(rt->filename)};
    int32_t track;
    vector<int32_t> self_track;
    file_req_n[rt->num] = 0;
    while (in >> track) {
        self_track.push_back(track);
        ++file_req_n[rt->num];
    }
    for (size_t i = 0; i < file_req_n[rt->num]; ++i) {
        int32_t xtrack = self_track[i];
        while (tasks.size() >= max_disk_queue || file_valid[rt->num])
            c_full.wait(mtx);
        tasks.push_back(make_pair(xtrack, rt->num));
        file_valid[rt->num] = true;
        print_request(rt->num, xtrack);
        c_empty.signal();
    }
    mtx.unlock();
}

void service(void*) {
    mtx.lock();
    while (true) {
        uint32_t max_req_n = min(max_disk_queue, max_file_n);
        if (max_req_n == 0 && tasks.size() == 0) {
            break;
        }
        while (tasks.size() < max_req_n) {
            c_full.broadcast();
            c_empty.wait(mtx);
            max_req_n = min(max_disk_queue, max_file_n);
        }
        decltype(tasks)::iterator mit;
        int32_t min_diff = 65536; // INT_MAX better
        for (auto it = tasks.begin(); it != tasks.end(); ++it) {
            if (abs(it->first - last_seek_pos) <= min_diff) {
                min_diff = abs(it->first - last_seek_pos);
                mit = it;
            }
        }
        print_service(mit->second, mit->first);
        last_seek_pos = mit->first;
        file_valid[mit->second] = false;
        ++file_ser_n[mit->second];
        if (file_req_n[mit->second] != -1 && file_ser_n[mit->second] == file_req_n[mit->second])
            --max_file_n;
        tasks.erase(mit);
        c_full.broadcast();
    }
    mtx.unlock();
}

void work(void*) {
    for (size_t i = 0; i < g_argc - 2; ++i) {
        thread tv{&request, &vr[i]};
    }
    thread sv{&service, nullptr};
    // sv.join();
}



int main(int argc, char* argv[]) {
    max_disk_queue = atoi(argv[1]);
    max_file_n = argc - 2;
    vr.resize(argc - 2);
    file_valid.resize(argc - 2, false);
    file_req_n.resize(argc - 2, -1);
    file_ser_n.resize(argc - 2, 0);
    g_argc = argc;
    for (size_t i = 0; i < argc - 2; ++i) {
        vr[i] = {i, argv[i + 2]};
    }
    cpu::boot(&work, nullptr, 0);
}