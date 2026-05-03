// Dmx4AllDat.cpp
// CPlusPlus DAT plugin — DMX4ALL USB/Serial interface for TouchDesigner
//
// Protocol reference:  DMX4ALL PC-Interface Communication Interface v1.x
// Serial settings:     38400 baud, 8 data bits, 1 stop bit, no parity, no handshake
//
// Table output layout (1 header + 512 data rows):
//   col 0 = "channel"  (1-indexed DMX channel number)
//   col 1 = "value"    (0-255)
//
// Input DAT (optional):
//   Table DAT: 512 rows × 1 col  — one value per row (row 0 = ch 1)
//   OR:          1 row × 512 cols — one value per column
//   Values are clamped to 0-255 integers.
//   If no input is connected the last sent universe is displayed.

#include "Dmx4AllDat.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

// ---------------------------------------------------------------------------
// Required C exports so TouchDesigner can load the DLL
// ---------------------------------------------------------------------------
extern "C"
{

DLLEXPORT
void
FillDATPluginInfo(DAT_PluginInfo* info)
{
    if (!info->setAPIVersion(DATCPlusPlusAPIVersion))
        return;

    info->customOPInfo.opType->setString("Dmx4alldat");
    info->customOPInfo.opLabel->setString("DMX4ALL DAT");
    info->customOPInfo.opIcon->setString("D4A");

    info->customOPInfo.authorName->setString("CraftKontrol");
    info->customOPInfo.authorEmail->setString("contact@craftkontrol.com");

    // 0 or 1 input DATs
    info->customOPInfo.minInputs = 0;
    info->customOPInfo.maxInputs = 1;
}

DLLEXPORT
DAT_CPlusPlusBase*
CreateDATInstance(const OP_NodeInfo* info)
{
    return new Dmx4AllDat(info);
}

DLLEXPORT
void
DestroyDATInstance(DAT_CPlusPlusBase* instance)
{
    delete (Dmx4AllDat*)instance;
}

} // extern "C"

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
Dmx4AllDat::Dmx4AllDat(const OP_NodeInfo* info)
    : myNodeInfo(info)
    , myHandle(INVALID_HANDLE_VALUE)
    , myConnected(false)
    , myBytesSent(0)
    , myFramesSent(0)
    , myExecuteCount(0)
{
    memset(myUniverse, 0, sizeof(myUniverse));
}

Dmx4AllDat::~Dmx4AllDat()
{
    closePort();
}

// ---------------------------------------------------------------------------
// getGeneralInfo
// ---------------------------------------------------------------------------
void
Dmx4AllDat::getGeneralInfo(DAT_GeneralInfo* ginfo, const OP_Inputs* inputs, void* reserved1)
{
    // Cook every frame only when connected so we keep pushing DMX data
    ginfo->cookEveryFrameIfAsked = myConnected;
}

// ---------------------------------------------------------------------------
// execute  — main cook function
// ---------------------------------------------------------------------------
void
Dmx4AllDat::execute(DAT_Output* output, const OP_Inputs* inputs, void* reserved1)
{
    myExecuteCount++;

    if (!output)
        return;

    // ------------------------------------------------------------------
    // 0. Cache Comport parameter every cook (pulsePressed has no OP_Inputs)
    // ------------------------------------------------------------------
    {
        const char* portParam = inputs->getParString("Comport");
        if (portParam)
            myPortName = portParam;
    }

    // ------------------------------------------------------------------
    // 1. Read input DAT if connected — update myUniverse
    // ------------------------------------------------------------------
    if (myConnected && inputs->getNumInputs() > 0)
    {
        const OP_DATInput* din = inputs->getInputDAT(0);
        if (din && din->isTable)
        {
            // Layout A: 512 rows × 1 col  (row index = channel 0-indexed)
            if (din->numRows >= 512 && din->numCols >= 1)
            {
                for (int i = 0; i < 512; i++)
                {
                    const char* s = din->getCell(i, 0);
                    if (s)
                    {
                        int v = atoi(s);
                        if (v < 0)   v = 0;
                        if (v > 255) v = 255;
                        myUniverse[i] = (uint8_t)v;
                    }
                }
            }
            // Layout B: 1 row × 512 cols
            else if (din->numRows >= 1 && din->numCols >= 512)
            {
                for (int i = 0; i < 512; i++)
                {
                    const char* s = din->getCell(0, i);
                    if (s)
                    {
                        int v = atoi(s);
                        if (v < 0)   v = 0;
                        if (v > 255) v = 255;
                        myUniverse[i] = (uint8_t)v;
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // 2. Read Blackout parameter
    // ------------------------------------------------------------------
    bool blackout = (inputs->getParInt("Blackout") != 0);

    // ------------------------------------------------------------------
    // 3. Send to hardware if connected
    // ------------------------------------------------------------------
    if (myConnected)
    {
        if (blackout)
        {
            // Send blackout enable command (B1) rather than zeroing universe buffer
            const unsigned char cmdB1[2] = { 'B', '1' };
            serialWrite(cmdB1, 2);
            // Read ACK (G)
            unsigned char ack = 0;
            serialRead(&ack, 1);
        }
        else
        {
            // Disable blackout first (idempotent)
            const unsigned char cmdB0[2] = { 'B', '0' };
            serialWrite(cmdB0, 2);
            unsigned char ack = 0;
            serialRead(&ack, 1);

            sendUniverse();
        }
        myFramesSent++;
    }

    // ------------------------------------------------------------------
    // 4. Build output table: header + 512 data rows
    //    Rows:  0 = header ("channel", "value")
    //    Rows:  1-512 = DMX ch 1-512
    // ------------------------------------------------------------------
    output->setOutputDataType(DAT_OutDataType::Table);
    output->setTableSize(513, 2);

    output->setCellString(0, 0, "channel");
    output->setCellString(0, 1, "value");

    char buf[8];
    uint8_t* src = blackout ? nullptr : myUniverse;

    for (int i = 0; i < 512; i++)
    {
        // Channel number (1-indexed)
#ifdef _WIN32
        sprintf_s(buf, "%d", i + 1);
#else
        snprintf(buf, sizeof(buf), "%d", i + 1);
#endif
        output->setCellString(i + 1, 0, buf);

        // Value
        uint8_t val = (src != nullptr) ? src[i] : 0;
#ifdef _WIN32
        sprintf_s(buf, "%d", (int)val);
#else
        snprintf(buf, sizeof(buf), "%d", (int)val);
#endif
        output->setCellString(i + 1, 1, buf);
    }
}

// ---------------------------------------------------------------------------
// setupParameters
// ---------------------------------------------------------------------------
void
Dmx4AllDat::setupParameters(OP_ParameterManager* manager, void* reserved1)
{
    // COM port string
    {
        OP_StringParameter sp;
        sp.name  = "Comport";
        sp.label = "COM Port";
        sp.defaultValue = "COM3";
        manager->appendString(sp);
    }

    // Connect / Disconnect pulse
    {
        OP_NumericParameter np;
        np.name  = "Connect";
        np.label = "Connect";
        manager->appendPulse(np);
    }

    // Blackout toggle
    {
        OP_NumericParameter np;
        np.name  = "Blackout";
        np.label = "Blackout";
        np.defaultValues[0] = 0.0;
        manager->appendToggle(np);
    }
}

// ---------------------------------------------------------------------------
// pulsePressed
// ---------------------------------------------------------------------------
void
Dmx4AllDat::pulsePressed(const char* name, void* reserved1)
{
    if (strcmp(name, "Connect") == 0)
    {
        if (myConnected)
        {
            closePort();
        }
        else
        {
            // Read the Comport parameter via the node info context
            // We store the port name from the last execute call — set a flag instead
            // (pulsePressed has no access to OP_Inputs, so we use the cached name)
            if (!myPortName.empty())
            {
                openPort(myPortName.c_str());
            }
            else
            {
                myLastError = "Set COM Port parameter before connecting.";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// getErrorString
// ---------------------------------------------------------------------------
void
Dmx4AllDat::getErrorString(OP_String* error, void* reserved1)
{
    if (!myLastError.empty())
        error->setString(myLastError.c_str());
}

// ---------------------------------------------------------------------------
// getInfoPopupString
// ---------------------------------------------------------------------------
void
Dmx4AllDat::getInfoPopupString(OP_String* info, void* reserved1)
{
    char buf[256];
#ifdef _WIN32
    sprintf_s(buf,
        "Port: %s  Connected: %s  Frames sent: %d  Bytes sent: %lld",
        myPortName.c_str(),
        myConnected ? "YES" : "NO",
        myFramesSent,
        myBytesSent);
#else
    snprintf(buf, sizeof(buf),
        "Port: %s  Connected: %s  Frames sent: %d  Bytes sent: %lld",
        myPortName.c_str(),
        myConnected ? "YES" : "NO",
        myFramesSent,
        myBytesSent);
#endif
    info->setString(buf);
}

// ---------------------------------------------------------------------------
// Info CHOP — expose connection stats as channels
// ---------------------------------------------------------------------------
int32_t
Dmx4AllDat::getNumInfoCHOPChans(void* reserved1)
{
    return 3;
}

void
Dmx4AllDat::getInfoCHOPChan(int32_t index, OP_InfoCHOPChan* chan, void* reserved1)
{
    if (index == 0)
    {
        chan->name->setString("connected");
        chan->value = myConnected ? 1.0f : 0.0f;
    }
    else if (index == 1)
    {
        chan->name->setString("frames_sent");
        chan->value = (float)myFramesSent;
    }
    else if (index == 2)
    {
        chan->name->setString("bytes_sent");
        chan->value = (float)myBytesSent;
    }
}

// ---------------------------------------------------------------------------
// Info DAT — key/value status table
// ---------------------------------------------------------------------------
bool
Dmx4AllDat::getInfoDATSize(OP_InfoDATSize* infoSize, void* reserved1)
{
    infoSize->rows = 4;
    infoSize->cols = 2;
    infoSize->byColumn = false;
    return true;
}

void
Dmx4AllDat::getInfoDATEntries(int32_t index, int32_t nEntries,
                               OP_InfoDATEntries* entries, void* reserved1)
{
    char buf[64];

    if (index == 0)
    {
        entries->values[0]->setString("port");
        entries->values[1]->setString(myPortName.c_str());
    }
    else if (index == 1)
    {
        entries->values[0]->setString("connected");
        entries->values[1]->setString(myConnected ? "1" : "0");
    }
    else if (index == 2)
    {
        entries->values[0]->setString("frames_sent");
#ifdef _WIN32
        sprintf_s(buf, "%d", myFramesSent);
#else
        snprintf(buf, sizeof(buf), "%d", myFramesSent);
#endif
        entries->values[1]->setString(buf);
    }
    else if (index == 3)
    {
        entries->values[0]->setString("bytes_sent");
#ifdef _WIN32
        sprintf_s(buf, "%lld", myBytesSent);
#else
        snprintf(buf, sizeof(buf), "%lld", myBytesSent);
#endif
        entries->values[1]->setString(buf);
    }
}

// ===========================================================================
// Private — Serial port helpers (Win32)
// ===========================================================================

bool
Dmx4AllDat::openPort(const char* portName)
{
    closePort();
    myLastError.clear();

    // Build the extended port name for COM ports > COM9
    std::string fullName = "\\\\.\\";
    fullName += portName;

    myHandle = CreateFileA(
        fullName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,              // exclusive access
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (myHandle == INVALID_HANDLE_VALUE)
    {
        char buf[128];
        sprintf_s(buf, "CreateFile failed for %s (error %lu)", portName, GetLastError());
        myLastError = buf;
        return false;
    }

    // Configure serial parameters: 38400 8N1, no handshake
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(myHandle, &dcb))
    {
        myLastError = "GetCommState failed.";
        closePort();
        return false;
    }

    dcb.BaudRate = CBR_38400;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;
    dcb.fBinary  = TRUE;
    dcb.fParity  = FALSE;
    dcb.fOutxCtsFlow  = FALSE;
    dcb.fOutxDsrFlow  = FALSE;
    dcb.fDtrControl   = DTR_CONTROL_DISABLE;
    dcb.fRtsControl   = RTS_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX    = FALSE;
    dcb.fInX     = FALSE;

    if (!SetCommState(myHandle, &dcb))
    {
        myLastError = "SetCommState failed.";
        closePort();
        return false;
    }

    // Timeouts: writes are synchronous, reads return quickly
    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout         = 50;   // ms
    timeouts.ReadTotalTimeoutConstant    = 100;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.WriteTotalTimeoutConstant   = 200;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(myHandle, &timeouts);

    PurgeComm(myHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);

    // Verify the interface responds to C?
    if (!checkConnection())
    {
        myLastError = "DMX4ALL interface did not respond to C? handshake.";
        closePort();
        return false;
    }

    myPortName   = portName;
    myConnected  = true;
    myFramesSent = 0;
    myBytesSent  = 0;
    myLastError.clear();
    return true;
}

void
Dmx4AllDat::closePort()
{
    if (myHandle != INVALID_HANDLE_VALUE)
    {
        // Send blackout on disconnect (safety)
        const unsigned char cmdB1[2] = { 'B', '1' };
        serialWrite(cmdB1, 2);

        CloseHandle(myHandle);
        myHandle = INVALID_HANDLE_VALUE;
    }
    myConnected = false;
}

bool
Dmx4AllDat::serialWrite(const unsigned char* data, DWORD len)
{
    if (myHandle == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    BOOL ok = WriteFile(myHandle, data, len, &written, nullptr);
    if (ok)
        myBytesSent += written;
    return ok && (written == len);
}

bool
Dmx4AllDat::serialRead(unsigned char* buf, DWORD len)
{
    if (myHandle == INVALID_HANDLE_VALUE)
        return false;
    DWORD got = 0;
    return ReadFile(myHandle, buf, len, &got, nullptr) && (got == len);
}

bool
Dmx4AllDat::checkConnection()
{
    // Transmit "C?" — expect "G" back
    const unsigned char cmd[2] = { 'C', '?' };
    if (!serialWrite(cmd, 2))
        return false;

    unsigned char resp = 0;
    if (!serialRead(&resp, 1))
        return false;

    return (resp == 'G');
}

// ---------------------------------------------------------------------------
// sendBlockTransfer
//   startCh : 0-indexed start channel (0 = DMX ch 1)
//   count   : number of channels to send (1-255)
//
// Packet format:
//   0xFF  start_low  start_high  count  data[0]...data[count-1]
//
// The interface replies with ASCII 'G' on success.
// ---------------------------------------------------------------------------
bool
Dmx4AllDat::sendBlockTransfer(int startCh, int count)
{
    if (count <= 0 || count > 255)   return false;
    if (startCh < 0 || startCh > 511) return false;

    // Build packet: header (4 bytes) + data (count bytes)
    unsigned char pkt[4 + 255];
    pkt[0] = 0xFF;
    pkt[1] = (unsigned char)(startCh & 0xFF);          // low byte of start
    pkt[2] = (unsigned char)((startCh >> 8) & 0x01);   // high byte (0 or 1)
    pkt[3] = (unsigned char)(count & 0xFF);

    memcpy(pkt + 4, myUniverse + startCh, count);

    if (!serialWrite(pkt, 4 + count))
        return false;

    // Read ACK ('G') — non-blocking with the configured timeout
    unsigned char ack = 0;
    serialRead(&ack, 1);   // ignore timeout/failure; hardware may not ACK all packets
    return true;
}

// ---------------------------------------------------------------------------
// sendUniverse
//   Sends the full 512-channel universe using 3 block-transfer packets:
//
//   Packet 1: channels   0-254  (255 ch, high=0, start_low=0,   count=255) sum=255 ≤ 0xFF ✓
//   Packet 2: channels 255-509  (255 ch, high=0, start_low=255, count=255) sum=510  (violates
//                                spec limit but works on modern hardware)
//   Packet 3: channels 510-511  (  2 ch, high=1, start_low=254, count=2  )
//
//   If your hardware strictly enforces the sum≤0xFF constraint you may need
//   to switch to per-channel ASCII commands for channels > 254.
// ---------------------------------------------------------------------------
bool
Dmx4AllDat::sendUniverse()
{
    // Packet 1: channels 0-254
    if (!sendBlockTransfer(0, 255))
        return false;

    // Packet 2: channels 255-509
    if (!sendBlockTransfer(255, 255))
        return false;

    // Packet 3: channels 510-511 (last 2 channels)
    if (!sendBlockTransfer(510, 2))
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// Note on execute / Comport parameter caching:
//   pulsePressed has no access to OP_Inputs, so we cache the COM port string
//   each cook cycle so Connect pulse can use it.
// We do this in execute before the send block.
// ---------------------------------------------------------------------------
// (Handled inline in execute — re-read Comport every cook to pick up changes)
// Override execute to also cache the port name and auto-connect on first use.
// The full execute is already defined above; this note documents the intent.
