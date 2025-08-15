#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>
#include <memory>

class Logger {
public:
    static void init(const std::string& logFilePath);
    static void info(const std::string& msg);
    static void error(const std::string& msg);
    static std::ofstream& getLogStream();

private:
    static std::unique_ptr<std::ofstream> logFile;
    static std::mutex mtx;
    static std::string getTimestamp();
};

#endif // LOGGER_H