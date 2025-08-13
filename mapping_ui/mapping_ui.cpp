#include <zmq.hpp>
#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <cstdlib>
#include <memory>
#include <thread>

std::string GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm time_info;
#if defined(_WIN32)
    localtime_s(&time_info, &time_t_now);
#else
    localtime_r(&time_t_now, &time_info);
#endif
    std::ostringstream oss;
    oss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

int main(int argc, char* argv[]) {
    zmq::context_t context(1);
    zmq::socket_t subscriber(context, zmq::socket_type::sub);

    subscriber.connect("tcp://localhost:5557");
    subscriber.set(zmq::sockopt::subscribe, "mapping");

    std::cout << "[Mapping UI] Subscribed to 'mapping' topic\n";

    // Optional sender: --send <UAV_NAME> enables stdin -> PUSH to UI command port
    std::unique_ptr<zmq::socket_t> push;
    std::string target = "UAV_1";
    if (argc >= 3 && std::string(argv[1]) == "--send") {
        target = argv[2];
        push = std::make_unique<zmq::socket_t>(context, zmq::socket_type::push);
        push->connect("tcp://localhost:5558");
        std::thread([p = push.get(), target]() {
            std::string line;
            while (std::getline(std::cin, line)) {
                const std::string msg = target + ":[mapping-ui] " + line;
                p->send(zmq::buffer(msg), zmq::send_flags::none);
            }
        }).detach();
        std::cout << "[Mapping UI] Sender enabled to " << target << " via 5558\n";
    }

    while (true) {
        zmq::message_t topic, data;
        if (!subscriber.recv(topic)) continue;
        if (!subscriber.recv(data)) continue;

        std::string msg(static_cast<char*>(data.data()), data.size());
        std::string topic_str(static_cast<char*>(topic.data()), topic.size());

        std::cout << "[" << GetTimestamp() << "] Topic: " << topic_str << " | Data: " << msg << std::endl;
    }
    return 0;
}
