#pragma once

#include <string>
#include "Instance.h"

class Parser {
public:
    /**
     * @brief Parses a Solomon-style instance file.
     * 
     * @param filename The absolute path to the instance file.
     * @return An Instance object populated with data from the file.
     */

    Parser(const std::string& filename);
    static std::shared_ptr<Instance> parse(const std::string& filename);
};
