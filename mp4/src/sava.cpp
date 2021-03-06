#include "daemon.hpp"

int Daemon::savaCompile() {
    string command;
    command = "cp ./mp4/tmp/client.cpp ./mp4/sava/client.cpp";
    system(command.c_str());
    command = "g++ -O3 -std=c++11 -o ./mp4/sava/runner ./mp4/sava/client.cpp ./mp4/sava/pregel.cpp ./mp4/sava/tcpsocket.cpp";
    try {
        system(command.c_str());
    } catch (...) {
        return 1;
    }
    return 0;
}

int Daemon::savaComplieApp(TCPSocket *sock) {
    // Ask for client code
    if (sock->recvFile("./mp4/sava/client.cpp") == 1) return 1;
    plog("Client code received.");
    // Compile client to get the binary
    if (savaCompile()) {
        sock->sendStr("error: cannot compile source code.");
        return 1;
    }
    plog("Client code compiled.");
    return 0;
}


/* TODO: This function is buggy */
int Daemon::savaReplicateMeta() {
    string ack, fname;
    for (auto x : master_list) {
        if (x.second == "Backup") {
            string msg = "savareplicatemeta;";
            msg += SAVA_APP_NAME + ";";
            msg += SAVA_INPUT + ";";
            msg += SAVA_OUTPUT + ";";
            msg += SAVA_COMBINATOR + ";";
            msg += to_string(SAVA_STATE) + ";";
            msg += to_string(SAVA_ROUND) + ";";
            plog("Try sending %s to %s", msg.c_str(), member_list[x.first].ip.c_str());
            TCPSocket sock_b(member_list[x.first].ip, BASEPORT + 4);
            if (sock_b.sendStr(msg)) {
                plog("Error in replicating meta data from primary to backup master");
                return 1;
            }
            sock_b.recvStr(ack);
            fname = "./mp4/sava/runner";
            if (sock_b.sendFile(fname.c_str())) {
                plog("Error in replicating meta data from primary to backup master");
                return 1;
            };
            sock_b.recvStr(ack);

            // We only have 1 standby master.
            return 0;
        }
    }
    return 0;
}

string Daemon::savaMasterGetTopResult(int topN, int rev) {
    string cmd, res;
    FILE *fp;
    int id, cnt;
    double val;
    priority_queue< KV, vector<KV>, greater<KV> > minHeap;
    priority_queue< KV > maxHeap;
    SAVA_WORKER_CONN.clear();
    for (int i = 1; i <= SAVA_NUM_WORKER; i++) {
        TCPSocket *sock_w = new TCPSocket(member_list[SAVA_WORKER_MAPPING[i]].ip, BASEPORT + 4);
        SAVA_WORKER_CONN[i] = sock_w;
    }
    
    cmd = "savaclientresult;";
    cmd += (rev == 0) ? "min;" : "max;";
    cmd += to_string(topN);

    for (auto x : SAVA_WORKER_CONN) {
        x.second->sendStr(cmd);
        x.second->recvFile("./mp4/tmp/tmptop");
        fp = fopen("./mp4/tmp/tmptop", "r");
        while (fscanf(fp, "%d %lf", &id, &val) != EOF) {
            if (rev) {
                maxHeap.push(KV(id, val));
            } else {
                minHeap.push(KV(id, val));
            }
        }
    }

    cnt = 0;
    if (rev) {
        while (!maxHeap.empty()) {
            cnt++;
            res += to_string(cnt) + ": (" + to_string(maxHeap.top().id) + ", " + to_string(maxHeap.top().value) + ")\n";
            if (cnt == topN) break;
            maxHeap.pop();
        }
    } else {
        while (!minHeap.empty()) {
            cnt++;
            res += to_string(cnt) + ": (" + to_string(minHeap.top().id) + ", " + to_string(minHeap.top().value) + ")\n";
            if (cnt == topN) break;
            minHeap.pop();
        }
    }
    return res;
}

int Daemon::savaTask(TCPSocket *sock, string app, string input, string output, string comb) {
    double total_time;
    string cmd, ack;

    SAVA_APP_NAME = app;
    SAVA_INPUT = input;
    SAVA_OUTPUT = output;
    SAVA_COMBINATOR = comb;
    SAVA_ROUND = 0;
    SAVA_STATE = 0; // Finish is 2;
    SAVA_GRAPH = 0; // Default is undirected.
    SAVA_NUM_MSG = 0;
    SAVA_NUM_WORKER = 0;
    SAVA_NUM_VERTICES = 0;
    SAVA_WORKER_ID = 0;
    if (SAVA_COMBINATOR == "") SAVA_COMBINATOR = "none";
    SAVA_VERTEX_MAPPING.clear();
    SAVA_WORKER_MAPPING.clear();
    SAVA_EDGES.clear();
    SAVA_WORKER_CONN.clear();

    total_time = 0;
    auto start = std::chrono::system_clock::now();
    // Partiton graph
    savaPartitionGraph();
    plog("Graph partition and distribution completed.");
    // Init parameters to workers
    savaInitPregelMaster();
    plog("Init worker subgraph.");
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> dif = end - start;
    total_time += dif.count();
    plog("Graph distribution time %f", dif.count());
    cmd = "Graph distribution time " + to_string(dif.count()) + "/" + to_string(total_time);
    sock->sendStr(cmd);
    sock->recvStr(ack);

    // Start superstep computation;
    while (SAVA_STATE != 2) {
        start = std::chrono::system_clock::now();
        if (savaMasterSuperstep()) {
            SAVA_STATE = 1;
            plog("Encounter error in %d round", SAVA_ROUND);
            break;
        }
        end = std::chrono::system_clock::now();
        dif = end - start;
        total_time += dif.count();
        plog("Round %d time %f", SAVA_ROUND, dif.count());
        cmd = "Round " + to_string(SAVA_ROUND) + " time " + to_string(dif.count()) + "/" + to_string(total_time);
        sock->sendStr(cmd);
        sock->recvStr(ack);
        SAVA_ROUND++;
    }

    if (SAVA_STATE == 1) {
        plog("About to stop");
        sock->sendStr("Task failed. Restarting");
        sock->recvStr(ack);
        usleep(5000000);
        plog("Current cluster size: %d", member_list.size());
        plog(membersToString());
        plog("Task failed. Restarting");
        //sock->sendStr("error");
        return 1;
    }
    //start = std::chrono::system_clock::now();
    if (prefixMatch(SAVA_OUTPUT, "TOP+")) {
        // Gather the topX(min) result
        int topN = stoi(SAVA_OUTPUT.substr(4, SAVA_OUTPUT.length()));
        string ans = savaMasterGetTopResult(topN, 0);
        if (sock->sendStr(ans)) plog("Error in sending answer.");
        sock->recvStr(ack);
    } else if (prefixMatch(SAVA_OUTPUT, "TOP-")) {
        // Gather the topX(max) result
        int topN = stoi(SAVA_OUTPUT.substr(4, SAVA_OUTPUT.length()));
        string ans = savaMasterGetTopResult(topN, 1);
        if (sock->sendStr(ans)) plog("Error in sending answer.");
        sock->recvStr(ack);
    } else {
        // Store it in SDFS
        sock->sendStr("Result stored in SDFS");
        sock->recvStr(ack);
    }
    //end = std::chrono::system_clock::now();
    //dif = end - start;
    //total_time += dif.count();
    //plog("Result processing time %f", SAVA_ROUND, dif.count());
    //cmd = "Result processing time " + to_string(dif.count()) + "/" + to_string(total_time);
    //sock->sendStr(cmd);
    //sock->recvStr(ack);
    sock->sendStr("finish");
    return 0;
    
}

void Daemon::sava() {
    while (leave_flag == false) {
        for (;;) {
            TCPSocket *sock = sava_socket.accept();
            std::thread msg_t(&Daemon::savaHandler, this, sock);
            msg_t.detach();
        }
    }
    plog("module sava exit");
}
