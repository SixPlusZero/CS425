#include "daemon.hpp"

void Daemon::savaHandler(TCPSocket *sock) {
    string info, cmd;
    int r;
    r = sock->recvStr(info);
    if (r == 1) {
        plog("Error in client. Close the connection.");
        delete sock;
        return;
    }
    plog("channel recv: %s", info.c_str());
    try {
        if (prefixMatch(info, "savanewtask")) {
            string app, input, output, comb, cmd;
            info = info.substr(12, info.length());
            app = info.substr(0, info.find(";"));
            info = info.substr(app.length()+1, info.length());
            input = info.substr(0, info.find(";"));
            info = info.substr(input.length()+1, info.length());
            if (info.find(";") == std::string::npos) {
                output = info;
            } else {
                output = info.substr(0, info.find(";"));
                info = info.substr(output.length()+1, info.length());
                comb = info;    
            }
            plog("New Task: %s %s %s %s %s", app.c_str(), input.c_str(), output.c_str(), comb.c_str());
            if (!hasFile(input)) {
                sock->sendStr("error: input file not found.");
                return;
            }
            if (hasFile(output)) {
                sock->sendStr("error: output file already exists.");
                return;
            }
            sock->sendStr("ack");
            savaTask(sock, app, input, output, comb);
            
            sock->sendStr("finish");

        } else if (prefixMatch(info, "savasendinputs")){
            string msg;
            sock->sendStr("ack");
            sock->recvFile("./mp4/sava/vertices.txt");
            sock->sendStr("ack");
            sock->recvFile("./mp4/sava/edges.txt");
            sock->sendStr("ack");
            sock->recvFile("./mp4/sava/runner");
            system("chmod 777 ./mp4/sava/runner");
            sock->sendStr("ack");
            sock->recvStr(msg);
            sock->sendStr("ack");
            SAVA_APP_NAME = msg.substr(0, msg.find(";"));
            SAVA_COMBINATOR = msg.substr(msg.find(";")+1, msg.length());
        } else if (prefixMatch(info, "savaclientinit")) {
            savaInitPregelClient();
            sock->sendStr("ack");
        } else if (prefixMatch(info, "savaclientstep")) {
            int step;
            step = stoi(info.substr(info.find(";")+1, info.length()));
            savaClientSuperstep(sock, step);
            
        } else {
            sock->sendStr("ack");
            return;
        }
    } catch (...) {
        plog("Error in SAVA execution");
        return;
    }
}