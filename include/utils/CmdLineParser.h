#ifndef CMDLINEPARSER_H
#define CMDLINEPARSER_H

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <sstream>

namespace utils {

class CmdLineParser {
public:
    CmdLineParser(int& argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            this->tokens.push_back(std::string(argv[i]));
        }
    }

    // Lấy giá trị của một option (ví dụ: --iterations 1000)
    template<typename T>
    T get(const std::string& option, const T& defaultValue) const {
        auto it = std::find(this->tokens.begin(), this->tokens.end(), option);
        if (it != this->tokens.end() && ++it != this->tokens.end()) {
            T value;
            std::stringstream ss(*it);
            ss >> value;
            if (ss.fail()) {
                // In thông báo lỗi nhưng vẫn trả về giá trị mặc định
                std::cerr << "Warning: Could not parse value for option " << option << ". Using default." << std::endl;
                return defaultValue;
            }
            return value;
        }
        return defaultValue;
    }

    // Kiểm tra sự tồn tại của một cờ (ví dụ: --no-logs)
    bool exists(const std::string& option) const {
        return std::find(this->tokens.begin(), this->tokens.end(), option) != this->tokens.end();
    }

    // Lấy đối số không phải là option (ví dụ: tên file)
    std::string getUnnamedArg(int index, const std::string& defaultValue = "") const {
        int currentIndex = 0;
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].rfind("--", 0) != 0) { // Nếu không bắt đầu bằng --
                if (currentIndex == index) {
                    return tokens[i];
                }
                currentIndex++;
            } else {
                // Bỏ qua giá trị của option
                if (i + 1 < tokens.size() && tokens[i+1].rfind("--", 0) != 0) {
                    i++;
                }
            }
        }
        return defaultValue;
    }


private:
    std::vector<std::string> tokens;
};

} // namespace utils

#endif //CMDLINEPARSER_H