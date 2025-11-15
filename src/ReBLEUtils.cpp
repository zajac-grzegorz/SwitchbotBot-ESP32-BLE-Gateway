#include "ReBLEUtils.h"

#include <Arduino.h>
#include <iomanip>
#include <cctype>

bool isValidHexChar(char c)
{
    return std::isxdigit(static_cast<unsigned char>(c));
}

std::vector<uint8_t> stringToHexArray(const std::string &hexString)
{
    std::vector<uint8_t> result;

    // Check even length
    if (hexString.length() % 2 != 0)
    {
        Serial.println("ReBLEUtils: Hex string length must be even");
        return result;
    }

    // Validate all characters
    for (char c : hexString)
    {
        if (!isValidHexChar(c))
        {
            Serial.printf("ReBLEUtils: Invalid hex character: %c\n", c);
            return result;
        }
    }

    // Convert to bytes
    for (size_t i = 0; i < hexString.length(); i += 2)
    {
        std::string byteString = hexString.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(byteString, nullptr, 16));
        result.push_back(byte);
    }

    return result;
}