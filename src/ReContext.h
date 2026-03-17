#pragma once

#include <string>

class ReContext
{
    public:

    ReContext() = default;
    
    bool getDoConnect() {
        return doConnect;
    }

    void setDoConnect(bool value) {
        doConnect = value;
    }
    
    std::string& getDoCommand() {
        return doCommand;
    }

    void setDoCommand(const std::string& value) {
        doCommand = value;
    }

    bool getBleDeviceFound() {
        return bleDeviceFound;
    }

    void setBleDeviceFound(bool value) {
        bleDeviceFound = value;
    }

    private:

    static bool bleDeviceFound;
    static bool doConnect;
    static std::string doCommand;
};

