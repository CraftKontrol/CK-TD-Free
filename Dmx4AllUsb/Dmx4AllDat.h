#pragma once

// Include windows.h first so HANDLE/DWORD are available before TD headers
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "DAT_CPlusPlusBase.h"
#include <string>
#include <cstdint>

using namespace TD;

// ---------------------------------------------------------------------------
// Dmx4AllDat — CPlusPlus DAT plugin for DMX4ALL USB/Serial interface
//
// Protocol: DMX4ALL ASCII + Array Transfer via virtual COM port
// Baud:     38400, 8N1
//
// Table output (513 rows):
//   Row 0:   "channel"  "value"
//   Row 1-512: channel number (1-indexed), current DMX value (0-255)
//
// Custom parameters:
//   Comport  (string)  — COM port name, e.g. "COM3"
//   Connect  (pulse)   — Open / close the serial port
//   Blackout (toggle)  — Force all channels to 0 without clearing universe
// ---------------------------------------------------------------------------
class Dmx4AllDat : public DAT_CPlusPlusBase
{
public:
    Dmx4AllDat(const OP_NodeInfo* info);
    virtual ~Dmx4AllDat();

    virtual void    getGeneralInfo(DAT_GeneralInfo*, const OP_Inputs*, void* reserved1) override;
    virtual void    execute(DAT_Output*, const OP_Inputs*, void* reserved1) override;

    virtual int32_t getNumInfoCHOPChans(void* reserved1) override;
    virtual void    getInfoCHOPChan(int32_t index, OP_InfoCHOPChan* chan, void* reserved1) override;

    virtual bool    getInfoDATSize(OP_InfoDATSize* infoSize, void* reserved1) override;
    virtual void    getInfoDATEntries(int32_t index, int32_t nEntries,
                                     OP_InfoDATEntries* entries, void* reserved1) override;

    virtual void    setupParameters(OP_ParameterManager* manager, void* reserved1) override;
    virtual void    pulsePressed(const char* name, void* reserved1) override;

    virtual void    getErrorString(OP_String* error, void* reserved1) override;
    virtual void    getInfoPopupString(OP_String* info, void* reserved1) override;

private:
    // Serial helpers
    bool    openPort(const char* portName);
    void    closePort();
    bool    serialWrite(const unsigned char* data, DWORD len);
    bool    serialRead(unsigned char* buf, DWORD len);
    bool    checkConnection();       // send C? and verify G response

    // DMX send helpers
    bool    sendBlockTransfer(int startCh, int count);   // 0-indexed, max 255 at a time
    bool    sendUniverse();          // sends all 512 channels in blocks

    // -----------------------------------------------------------------------
    const OP_NodeInfo*  myNodeInfo;

    HANDLE              myHandle;       // Win32 serial handle
    bool                myConnected;

    uint8_t             myUniverse[512];    // current DMX values, 0-indexed (ch 0 = DMX ch 1)

    std::string         myPortName;
    std::string         myLastError;

    // Stats
    int64_t             myBytesSent;
    int32_t             myFramesSent;
    int32_t             myExecuteCount;
};
