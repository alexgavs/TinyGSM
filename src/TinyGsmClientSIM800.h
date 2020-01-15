/**
 * @file       TinyGsmClientSIM800.h
 * @author     Volodymyr Shymanskyy
 * @license    LGPL-3.0
 * @copyright  Copyright (c) 2016 Volodymyr Shymanskyy
 * @date       Nov 2016
 */

#ifndef TinyGsmClientSIM800_h
#define TinyGsmClientSIM800_h
#pragma message("TinyGSM:  TinyGsmClientSIM800")

//#define TINY_GSM_DEBUG Serial
//#define TINY_GSM_USE_HEX

#if !defined(TINY_GSM_RX_BUFFER)
  #define TINY_GSM_RX_BUFFER 64
#endif

#define TINY_GSM_MUX_COUNT 5

#ifndef TINY_GSM_PHONEBOOK_RESULTS
  #define TINY_GSM_PHONEBOOK_RESULTS 5
#endif

#include <TinyGsmCommon.h>

#define GSM_NL "\r\n"
static const char GSM_OK[] TINY_GSM_PROGMEM = "OK" GSM_NL;
static const char GSM_ERROR[] TINY_GSM_PROGMEM = "ERROR" GSM_NL;

enum SimStatus {
  SIM_ERROR = 0,
  SIM_READY = 1,
  SIM_LOCKED = 2,
};

enum RegStatus {
  REG_UNREGISTERED = 0,
  REG_SEARCHING    = 2,
  REG_DENIED       = 3,
  REG_OK_HOME      = 1,
  REG_OK_ROAMING   = 5,
  REG_UNKNOWN      = 4,
};

enum TinyGSMDateTimeFormat {
  DATE_FULL = 0,
  DATE_TIME = 1,
  DATE_DATE = 2
};

enum class PhonebookStorageType : uint8_t {
  SIM,    // Typical size: 250
  Phone,  // Typical size: 100
  Invalid
};

struct PhonebookStorage {
  PhonebookStorageType type = PhonebookStorageType::Invalid;
  uint8_t used  = {0};
  uint8_t total = {0};
};

enum NumerCallerType{
  NATIONAL=129,
  INTERNATIONAL=145
};

struct PhonebookEntry {
  String number;
  String text;
  String type;
  //NumerCallerType type;
};

struct PhonebookMatches {
  uint8_t index[TINY_GSM_PHONEBOOK_RESULTS] = {0};
};

enum class MessageStorageType : uint8_t {
  SIM,                // SM
  Phone,              // ME
  SIMPreferred,       // SM_P
  PhonePreferred,     // ME_P
  Either_SIMPreferred // MT (use both)
};

struct MessageStorage {
  /*
   * [0]: Messages to be read and deleted from this memory storage
   * [1]: Messages will be written and sent to this memory storage
   * [2]: Received messages will be placed in this memory storage
   */
  MessageStorageType type[3];
  uint8_t used[3]  = {0};
  uint8_t total[3] = {0};
};


enum class DeleteAllSmsMethod : uint8_t {
  Read     = 1,
  Unread   = 2,
  Sent     = 3,
  Unsent   = 4,
  Received = 5,
  All      = 6
};


class TinyGsmSim800
{

public:

class GsmClient : public Client
{
  friend class TinyGsmSim800;
  typedef TinyGsmFifo<uint8_t, TINY_GSM_RX_BUFFER> RxFifo;

public:
  GsmClient() {}

  GsmClient(TinyGsmSim800& modem, uint8_t mux = 1) {
    init(&modem, mux);
  }

  virtual ~GsmClient(){}

  bool init(TinyGsmSim800* modem, uint8_t mux = 1) {
    this->at = modem;
    this->mux = mux;
    sock_available = 0;
    prev_check = 0;
    sock_connected = false;
    got_data = false;

    at->sockets[mux] = this;

    return true;
  }

public:
  virtual int connect(const char *host, uint16_t port, int timeout_s) {
    stop();
    TINY_GSM_YIELD();
    rx.clear();
    sock_connected = at->modemConnect(host, port, mux, false, timeout_s);
    return sock_connected;
  }

TINY_GSM_CLIENT_CONNECT_OVERLOADS()

  virtual void stop(uint32_t maxWaitMs) {
    TINY_GSM_CLIENT_DUMP_MODEM_BUFFER()
    at->sendAT(GF("+CIPCLOSE="), mux, GF(",1"));  // Quick close
    sock_connected = false;
    at->waitResponse();
  }

  virtual void stop() { stop(15000L); }

TINY_GSM_CLIENT_WRITE()

TINY_GSM_CLIENT_AVAILABLE_WITH_BUFFER_CHECK()

TINY_GSM_CLIENT_READ_WITH_BUFFER_CHECK()

TINY_GSM_CLIENT_PEEK_FLUSH_CONNECTED()

  /*
   * Extended API
   */

  String remoteIP() TINY_GSM_ATTR_NOT_IMPLEMENTED;

private:
  TinyGsmSim800*  at;
  uint8_t         mux;
  uint16_t        sock_available;
  uint32_t        prev_check;
  bool            sock_connected;
  bool            got_data;
  RxFifo          rx;
};


class GsmClientSecure : public GsmClient
{
public:
  GsmClientSecure() {}

  GsmClientSecure(TinyGsmSim800& modem, uint8_t mux = 1)
    : GsmClient(modem, mux)
  {}

  virtual ~GsmClientSecure(){}

public:
  virtual int connect(const char *host, uint16_t port, int timeout_s) {
    stop();
    TINY_GSM_YIELD();
    rx.clear();
    sock_connected = at->modemConnect(host, port, mux, true, timeout_s);
    return sock_connected;
  }
};


public:

  TinyGsmSim800(Stream& stream)
    : stream(stream)
  {
    memset(sockets, 0, sizeof(sockets));
  }

  virtual ~TinyGsmSim800() {}

  /*
   * Basic functions
   */

  bool begin(const char* pin = NULL) {
    return init(pin);
  }

  bool init(const char* pin = NULL) {
    DBG(GF("### TinyGSM Version:"), TINYGSM_VERSION);

    if (!testAT()) {
      return false;
    }
    sendAT(GF("&FZ"));  // Factory + Reset
    waitResponse();
    sendAT(GF("E0"));   // Echo Off
    if (waitResponse() != 1) {
      return false;
    }

    DBG(GF("### Modem:"), getModemName());

    int ret = getSimStatus();
    // if the sim isn't ready and a pin has been provided, try to unlock the sim
    if (ret != SIM_READY && pin != NULL && strlen(pin) > 0) {
      simUnlock(pin);
      return (getSimStatus() == SIM_READY);
    }
    // if the sim is ready, or it's locked but no pin has been provided, return true
    else {
      return (ret == SIM_READY || ret == SIM_LOCKED);
    }
  }

  String getModemName() {
    String name = "";
    #if defined(TINY_GSM_MODEM_SIM800)
      name = "SIMCom SIM800";
    #elif defined(TINY_GSM_MODEM_SIM808)
      name = "SIMCom SIM808";
    #elif defined(TINY_GSM_MODEM_SIM868)
      name = "SIMCom SIM868";
    #elif defined(TINY_GSM_MODEM_SIM900)
      name = "SIMCom SIM900";
    #endif

    sendAT(GF("+GMM"));
    String res2;
    if (waitResponse(1000L, res2) != 1) {
      return name;
    }
    res2.replace(GSM_NL "OK" GSM_NL, "");
    res2.replace("_", " ");
    res2.trim();

    name = res2;
    DBG("### Modem:", name);
    return name;
  }

TINY_GSM_MODEM_SET_BAUD_IPR()

TINY_GSM_MODEM_TEST_AT()

TINY_GSM_MODEM_MAINTAIN_CHECK_SOCKS()

  bool factoryDefault() {
    sendAT(GF("&FZE0&W"));  // Factory + Reset + Echo Off + Write
    waitResponse();
    sendAT(GF("+IPR=0"));   // Auto-baud
    waitResponse();
    sendAT(GF("+IFC=0,0")); // No Flow Control
    waitResponse();
    sendAT(GF("+ICF=3,3")); // 8 data 0 parity 1 stop
    waitResponse();
    sendAT(GF("+CSCLK=0")); // Disable Slow Clock
    waitResponse();
    sendAT(GF("&W"));       // Write configuration
    return waitResponse() == 1;
  }

TINY_GSM_MODEM_GET_INFO_ATI()

  bool hasSSL() {
#if defined(TINY_GSM_MODEM_SIM900)
    return false;
#else
    sendAT(GF("+CIPSSL=?"));
    if (waitResponse(GF(GSM_NL "+CIPSSL:")) != 1) {
      return false;
    }
    return waitResponse() == 1;
#endif
  }

  bool hasWifi() {
    return false;
  }

  bool hasGPRS() {
    return true;
  }

  /*
   * Power functions
   */

  bool restart() {
    if (!testAT()) {
      return false;
    }
    //Enable Local Time Stamp for getting network time
    // TODO: Find a better place for this
    sendAT(GF("+CLTS=1"));
    if (waitResponse(10000L) != 1) {
      return false;
    }
    sendAT(GF("&W"));
    waitResponse();
    sendAT(GF("+CFUN=0"));
    if (waitResponse(10000L) != 1) {
      return false;
    }
    sendAT(GF("+CFUN=1,1"));
    if (waitResponse(10000L) != 1) {
      return false;
    }
    delay(3000);
    return init();
  }

  bool poweroff() {
    sendAT(GF("+CPOWD=1"));
    return waitResponse(10000L, GF("NORMAL POWER DOWN")) == 1;
  }

  bool radioOff() {
    sendAT(GF("+CFUN=0"));
    if (waitResponse(10000L) != 1) {
      return false;
    }
    delay(3000);
    return true;
  }

  /*
    During sleep, the SIM800 module has its serial communication disabled. In order to reestablish communication
    pull the DRT-pin of the SIM800 module LOW for at least 50ms. Then use this function to disable sleep mode.
    The DTR-pin can then be released again.
  */
  bool sleepEnable(bool enable = true) {
    sendAT(GF("+CSCLK="), enable);
    return waitResponse() == 1;
  }

bool netlightEnable(bool enable = true) {
      sendAT(GF("+CNETLIGHT="), enable);
      bool ok = waitResponse() == 1;

      sendAT(GF("+CSGS="), enable);
      ok &= waitResponse() == 1;

      return ok;
  }
  /*
   * SIM card functions
   */

TINY_GSM_MODEM_SIM_UNLOCK_CPIN()

TINY_GSM_MODEM_GET_SIMCCID_CCID()

TINY_GSM_MODEM_GET_IMEI_GSN()

  SimStatus getSimStatus(unsigned long timeout_ms = 10000L) {
    for (unsigned long start = millis(); millis() - start < timeout_ms; ) {
      sendAT(GF("+CPIN?"));
      if (waitResponse(GF(GSM_NL "+CPIN:")) != 1) {
        delay(1000);
        continue;
      }
      int status = waitResponse(GF("READY"), GF("SIM PIN"), GF("SIM PUK"), GF("NOT INSERTED"), GF("NOT READY"));
      waitResponse();
      switch (status) {
        case 2:
        case 3:  return SIM_LOCKED;
        case 1:  return SIM_READY;
        default: return SIM_ERROR;
      }
    }
    return SIM_ERROR;
  }

TINY_GSM_MODEM_GET_REGISTRATION_XREG(CREG)

TINY_GSM_MODEM_GET_OPERATOR_COPS()

  /*
   * Generic network functions
   */

TINY_GSM_MODEM_GET_CSQ()

  bool isNetworkConnected() {
    RegStatus s = getRegistrationStatus();
    return (s == REG_OK_HOME || s == REG_OK_ROAMING);
  }

TINY_GSM_MODEM_WAIT_FOR_NETWORK()

  /*
   * GPRS functions
   */

  bool gprsConnect(const char* apn, const char* user = NULL, const char* pwd = NULL) {
    gprsDisconnect();

    // Set the Bearer for the IP
    sendAT(GF("+SAPBR=3,1,\"Contype\",\"GPRS\""));  // Set the connection type to GPRS
    waitResponse();

    sendAT(GF("+SAPBR=3,1,\"APN\",\""), apn, '"');  // Set the APN
    waitResponse();

    if (user && strlen(user) > 0) {
      sendAT(GF("+SAPBR=3,1,\"USER\",\""), user, '"');  // Set the user name
      waitResponse();
    }
    if (pwd && strlen(pwd) > 0) {
      sendAT(GF("+SAPBR=3,1,\"PWD\",\""), pwd, '"');  // Set the password
      waitResponse();
    }

    // Define the PDP context
    sendAT(GF("+CGDCONT=1,\"IP\",\""), apn, '"');
    waitResponse();

    // Activate the PDP context
    sendAT(GF("+CGACT=1,1"));
    waitResponse(60000L);

    // Open the definied GPRS bearer context
    sendAT(GF("+SAPBR=1,1"));
    waitResponse(85000L);
    // Query the GPRS bearer context status
    sendAT(GF("+SAPBR=2,1"));
    if (waitResponse(30000L) != 1)
      return false;

    // Attach to GPRS
    sendAT(GF("+CGATT=1"));
    if (waitResponse(60000L) != 1)
      return false;

    // TODO: wait AT+CGATT?

    // Set to multi-IP
    sendAT(GF("+CIPMUX=1"));
    if (waitResponse() != 1) {
      return false;
    }

    // Put in "quick send" mode (thus no extra "Send OK")
    sendAT(GF("+CIPQSEND=1"));
    if (waitResponse() != 1) {
      return false;
    }

    // Set to get data manually
    sendAT(GF("+CIPRXGET=1"));
    if (waitResponse() != 1) {
      return false;
    }

    // Start Task and Set APN, USER NAME, PASSWORD
    sendAT(GF("+CSTT=\""), apn, GF("\",\""), user, GF("\",\""), pwd, GF("\""));
    if (waitResponse(60000L) != 1) {
      return false;
    }

    // Bring Up Wireless Connection with GPRS or CSD
    sendAT(GF("+CIICR"));
    if (waitResponse(60000L) != 1) {
      return false;
    }

    // Get Local IP Address, only assigned after connection
    sendAT(GF("+CIFSR;E0"));
    if (waitResponse(10000L) != 1) {
      return false;
    }

    // Configure Domain Name Server (DNS)
    sendAT(GF("+CDNSCFG=\"8.8.8.8\",\"8.8.4.4\""));
    if (waitResponse() != 1) {
      return false;
    }

    return true;
  }

  bool gprsDisconnect() {
    // Shut the TCP/IP connection
    // CIPSHUT will close *all* open connections
    sendAT(GF("+CIPSHUT"));
    if (waitResponse(60000L) != 1)
      return false;

    sendAT(GF("+CGATT=0"));  // Deactivate the bearer context
    if (waitResponse(60000L) != 1)
      return false;

    return true;
  }

  bool isGprsConnected() {
    sendAT(GF("+CGATT?"));
    if (waitResponse(GF(GSM_NL "+CGATT:")) != 1) {
      return false;
    }
    int res = stream.readStringUntil('\n').toInt();
    waitResponse();
    if (res != 1)
      return false;

    sendAT(GF("+CIFSR;E0")); // Another option is to use AT+CGPADDR=1
    if (waitResponse() != 1)
      return false;

    return true;
  }

  /*
   * IP Address functions
   */

  String getLocalIP() {
    sendAT(GF("+CIFSR;E0"));
    String res;
    if (waitResponse(10000L, res) != 1) {
      return "";
    }
    res.replace(GSM_NL "OK" GSM_NL, "");
    res.replace(GSM_NL, "");
    res.trim();
    return res;
  }

  IPAddress localIP() {
    return TinyGsmIpFromString(getLocalIP());
  }

  /*
   * Phone Call functions
   */

  bool setGsmBusy(bool busy = true) {
    sendAT(GF("+GSMBUSY="), busy ? 1 : 0);
    return waitResponse() == 1;
  }

  bool callAnswer() {
    sendAT(GF("A"));
    return waitResponse() == 1;
  }

  // Returns true on pick-up, false on error/busy
  bool callNumber(const String& number) {
    if (number == GF("last")) {
      sendAT(GF("DL"));
    } else {
      sendAT(GF("D"), number, ";");
    }
    int status = waitResponse(60000L,
                              GFP(GSM_OK),
                              GF("BUSY" GSM_NL),
                              GF("NO ANSWER" GSM_NL),
                              GF("NO CARRIER" GSM_NL));
    switch (status) {
    case 1:  return true;
    case 2:
    case 3:  return false;
    default: return false;
    }
  }

  bool callHangup() {
    sendAT(GF("H"));
    return waitResponse() == 1;
}
  bool receiveCallerIdentification(const bool receive) {
    sendAT(GF("+CLIP="), receive); // Calling Line Identification Presentation

    // Unsolicited result code format:
    // +CLIP: <number>,<type>[,<subaddr>,<satype>,<alphaId>,<CLIvalidity>]

    return waitResponse(15000L) == 1;
  }

  // 0-9,*,#,A,B,C,D
  bool dtmfSend(char cmd, int duration_ms = 100) {
    duration_ms = constrain(duration_ms, 100, 1000);

    sendAT(GF("+VTD="), duration_ms / 100); // VTD accepts in 1/10 of a second
    waitResponse();

    sendAT(GF("+VTS="), cmd);
    return waitResponse(10000L) == 1;
  }

  /*
   * Messaging functions
   * 
   * AT+CMGD Delete SMS message
   * AT+CMGF Select SMS message format
   * AT+CMGL List SMS messages from preferred store
   * AT+CMGR Read SMS message
   * AT+CMGS Send SMS message
   * AT+CMGW Write SMS message to memory
   * AT+CMSS Send SMS message from storage
   * AT+CNMI New SMS message indications
   * AT+CPMS Preferred SMS message storage
   * AT+CRES Restore SMS settings
   * AT+CSAS Save SMS settings
   * AT+CSCA SMS service center address
   * AT+CSCB Select cell broadcast SMS messages
   * AT+CSDH Show SMS text mode parameters
   * AT+CSMP Set SMS text mode parameters
   * AT+CSMS Select message service
   */

  String sendUSSD(const String& code) {
    sendAT(GF("+CMGF=1"));
    waitResponse();
    sendAT(GF("+CSCS=\"HEX\""));
    waitResponse();
    sendAT(GF("+CUSD=1,\""), code, GF("\""));
    if (waitResponse() != 1) {
      return "";
    }
    if (waitResponse(10000L, GF(GSM_NL "+CUSD:")) != 1) {
      return "";
    }
    stream.readStringUntil('"');
    String hex = stream.readStringUntil('"');
    stream.readStringUntil(',');
    int dcs = stream.readStringUntil('\n').toInt();

    if (dcs == 15) {
      return TinyGsmDecodeHex8bit(hex);
    } else if (dcs == 72) {
      return TinyGsmDecodeHex16bit(hex);
    } else {
      return hex;
    }
  }

  bool sendSMS(const String& number, const String& text) {
    sendAT(GF("+CMGF=1"));
    waitResponse();
    //Set GSM 7 bit default alphabet (3GPP TS 23.038)
    sendAT(GF("+CSCS=\"GSM\""));
    waitResponse();
    sendAT(GF("+CMGS=\""), number, GF("\""));
    if (waitResponse(GF(">")) != 1) {
      return false;
    }
    stream.print(text);
    stream.write((char)0x1A);
    stream.flush();
    return waitResponse(60000L) == 1;
  }

  bool sendSMS_UTF16(const String& number, const void* text, size_t len) {
    sendAT(GF("+CMGF=1"));
    waitResponse();
    sendAT(GF("+CSCS=\"HEX\""));
    waitResponse();
    sendAT(GF("+CSMP=17,167,0,8"));
    waitResponse();

    sendAT(GF("+CMGS=\""), number, GF("\""));
    if (waitResponse(GF(">")) != 1) {
      return false;
    }

    uint16_t* t = (uint16_t*)text;
    for (size_t i=0; i<len; i++) {
      uint8_t c = t[i] >> 8;
      if (c < 0x10) { stream.print('0'); }
      stream.print(c, HEX);
      c = t[i] & 0xFF;
      if (c < 0x10) { stream.print('0'); }
      stream.print(c, HEX);
    }
    stream.write((char)0x1A);
    stream.flush();
    return waitResponse(60000L) == 1;
  }

/*
Sms readSmsMessage(const uint8_t index, const bool changeStatusToRead = true) {
    sendAT(GF("+CMGR="), index, GF(","), static_cast<const uint8_t>(!changeStatusToRead)); // Read SMS Message
    if (waitResponse(5000L, GF(GSM_NL "+CMGR: \"")) != 1) {
      stream.readString();
      return {};
    }
//work

    Sms sms;

    // AT reply:
    // <stat>,<oa>[,<alpha>],<scts>[,<tooa>,<fo>,<pid>,<dcs>,<sca>,<tosca>,<length>]<CR><LF><data>

    //<stat>
    const String res = stream.readStringUntil('"');
    if (res == GF("REC READ")) {
      sms.status = SmsStatus::REC_READ;
    } else if (res == GF("REC UNREAD")) {
      sms.status = SmsStatus::REC_UNREAD;
    } else if (res == GF("STO UNSENT")) {
      sms.status = SmsStatus::STO_UNSENT;
    } else if (res == GF("STO SENT")) {
      sms.status = SmsStatus::STO_SENT;
    } else if (res == GF("ALL")) {
      sms.status = SmsStatus::ALL;
    } else {
      stream.readString();
      return {};
    }

    // <oa>
    streamSkipUntil('"');
    sms.originatingAddress = stream.readStringUntil('"');

    // <alpha>
    streamSkipUntil('"');
    sms.phoneBookEntry = stream.readStringUntil('"');

    // <scts>
    streamSkipUntil('"');
    sms.serviceCentreTimeStamp = stream.readStringUntil('"');
    streamSkipUntil(',');

    streamSkipUntil(','); // <tooa>
    streamSkipUntil(','); // <fo>
    streamSkipUntil(','); // <pid>

    // <dcs>
    const uint8_t alphabet = (stream.readStringUntil(',').toInt() >> 2) & B11;
    switch (alphabet) {
    case B00:
      sms.alphabet = SmsAlphabet::GSM_7bit;
      break;
    case B01:
      sms.alphabet = SmsAlphabet::Data_8bit;
      break;
    case B10:
      sms.alphabet = SmsAlphabet::UCS2;
      break;
    case B11:
    default:
      sms.alphabet = SmsAlphabet::Reserved;
      break;
    }

    streamSkipUntil(','); // <sca>
    streamSkipUntil(','); // <tosca>

    // <length>, CR, LF
    const long length = stream.readStringUntil('\n').toInt();

    // <data>
    String data = stream.readString();
    data.remove(static_cast<const unsigned int>(length));
    switch (sms.alphabet) {
    case SmsAlphabet::GSM_7bit:
      sms.message = data;
      break;
    case SmsAlphabet::Data_8bit:
      sms.message = TinyGsmDecodeHex8bit(data);
      break;
    case SmsAlphabet::UCS2:
      sms.message = TinyGsmDecodeHex16bit(data);
      break;
    case SmsAlphabet::Reserved:
      return {};
    }

    return sms;
  }

*/


Sms readSmsMessage2(const uint8_t index, const bool changeStatusToRead = true) {
  /*
      +CMGR: "REC READ","+972509468305","ALEX","20/01/11,22:37:40+08"
  */
  
    sendAT(GF("+CMGR="), index, GF(","), static_cast<const uint8_t>(!changeStatusToRead)); // Read SMS Message
    if (waitResponse(5000L, GF(GSM_NL "+CMGR: \"")) != 1) {
      stream.readString();
      return {};
    }

    Sms sms;


/*

at+cmgf=0
OK

at+cmgr=2
+CMGR: 1,"",28
0791795268721904040C917952906438500000021031108341800A31D98C56B3DD703918

OK

at+cmgf=1
OK

at+cmgr=2
+CMGR: "REC READ","+972509468305","","20/01/13,01:38:14+08"
1234567890


*/


    // AT reply:
    // "REC READ","+972509468305","ALEX","20/01/11,22:37:40+08"

    // <stat>,<oa>[,<alpha>],<scts>[,<tooa>,<fo>,<pid>,<dcs>,<sca>,<tosca>,<length>]<CR><LF><data>

    //<stat>

    const String res = stream.readStringUntil('"');
    

           if (res == (String) _statSMS.REC_READ) {
      sms.status = SmsStatus::REC_READ;
    } else if (res == (String) _statSMS.REC_UNREAD) {
      sms.status = SmsStatus::REC_UNREAD;
    } else if (res == (String) _statSMS.STO_UNSENT) {
      sms.status = SmsStatus::STO_UNSENT;
    } else if (res == (String) _statSMS.STO_SENT) {
      sms.status = SmsStatus::STO_SENT;
    } else if (res == (String) _statSMS.ALL) {
      sms.status = SmsStatus::ALL;
    } else {
      stream.readString();
      return {};
    }




/*
      if (res == GF("REC READ")) {
      sms.status = SmsStatus::REC_READ;
    } else if (res == GF("REC UNREAD")) {
      sms.status = SmsStatus::REC_UNREAD;
    } else if (res == GF("STO UNSENT")) {
      sms.status = SmsStatus::STO_UNSENT;
    } else if (res == GF("STO SENT")) {
      sms.status = SmsStatus::STO_SENT;
    } else if (res == GF("ALL")) {
      sms.status = SmsStatus::ALL;
    } else {
      stream.readString();
      return {};
    }

*/


    // Debug for internal use
    SerialMon.print("restype:");
    SerialMon.println(res);



    // <oa> //phone sms sender
    streamSkipUntil('"');
    sms.originatingAddress = stream.readStringUntil('"');

    SerialMon.print("ResPhone:");
    SerialMon.println(sms.originatingAddress);
// restype:REC READ
// resphone:+972509468305

// +CMGR: "REC READ","+972509468305","","20/01/13,01:38:14+08"

    // <alpha>  
    streamSkipUntil('"');
    sms.phoneBookEntry = stream.readStringUntil('"');

    SerialMon.print("phoneBookEntry:");
    SerialMon.println(sms.phoneBookEntry);

    // <scts>
    streamSkipUntil('"');
    sms.serviceCentreTimeStamp = stream.readStringUntil('"');

    SerialMon.print("serviceCentreTimeStamp:");
    SerialMon.println(sms.phoneBookEntry);


    streamSkipUntil(',');

    streamSkipUntil(','); // <tooa>
    streamSkipUntil(','); // <fo>
    streamSkipUntil(','); // <pid>

    // <dcs>
    const uint8_t alphabet = (stream.readStringUntil(',').toInt() >> 2) & B11;
    switch (alphabet) {
    case B00:
      sms.alphabet = SmsAlphabet::GSM_7bit;
      DBG("A:7bit");
      break;
    case B01:
      sms.alphabet = SmsAlphabet::Data_8bit;
      DBG("A:8bit");
      break;
    case B10:
      sms.alphabet = SmsAlphabet::UCS2;
      DBG("A:UCS2");
      break;
    case B11:
      default:
      sms.alphabet = SmsAlphabet::Reserved;
      DBG("A:Reserver");
      break;
    }


    streamSkipUntil(','); // <sca>
    streamSkipUntil(','); // <tosca>

    // <length>, CR, LF
    const long length = stream.readStringUntil('\n').toInt();
    DBG("Length",length);



    // <data>
    String data = stream.readString();

    SerialMon.print("data:");
    SerialMon.println(data);


    data.remove(static_cast<const unsigned int>(length));
    switch (sms.alphabet) {
    case SmsAlphabet::GSM_7bit:
      sms.message = data;
      break;
    case SmsAlphabet::Data_8bit:
      sms.message = TinyGsmDecodeHex8bit(data);
      break;
    case SmsAlphabet::UCS2:
      sms.message = TinyGsmDecodeHex16bit(data);
      break;
    case SmsAlphabet::Reserved:
      return {};
    }

    return sms;
  }

  MessageStorage getPreferredMessageStorage() {
    sendAT(GF("+CPMS?")); // Preferred SMS Message Storage
    if (waitResponse(GF(GSM_NL "+CPMS:")) != 1) {
      stream.readString();
      return {};
    }

    // AT reply:
    // +CPMS: <mem1>,<used1>,<total1>,<mem2>,<used2>,<total2>,<mem3>,<used3>,<total3>

    MessageStorage messageStorage;
    for (uint8_t i = 0; i < 3; ++i) {
      // type
      streamSkipUntil('"');
      const String mem = stream.readStringUntil('"');
      if (mem == GF("SM")) {
        messageStorage.type[i] = MessageStorageType::SIM;
      } else if (mem == GF("ME")) {
        messageStorage.type[i] = MessageStorageType::Phone;
      } else if (mem == GF("SM_P")) {
        messageStorage.type[i] = MessageStorageType::SIMPreferred;
      } else if (mem == GF("ME_P")) {
        messageStorage.type[i] = MessageStorageType::PhonePreferred;
      } else if (mem == GF("MT")) {
        messageStorage.type[i] = MessageStorageType::Either_SIMPreferred;
      } else {
        stream.readString();
        return {};
      }

      // used
      streamSkipUntil(',');
      messageStorage.used[i] = static_cast<uint8_t>(stream.readStringUntil(',').toInt());

      // total
      if (i < 2) {
        messageStorage.total[i] = static_cast<uint8_t>(stream.readStringUntil(',').toInt());
      } else {
        messageStorage.total[i] = static_cast<uint8_t>(stream.readString().toInt());
      }
    }

    return messageStorage;
  }

  bool setPreferredMessageStorage(const MessageStorageType type[3]) {
    //const auto convertMstToString = [](const MessageStorageType &type) { 
     //const auto convertMstToString = [](const MessageStorageType &type) -> auto {
      const auto convertMstToString = [](const MessageStorageType &type) -> const __FlashStringHelper* {
      switch (type) {
      case MessageStorageType::SIM:
        return GF("\"SM\"");
      case MessageStorageType::Phone:
        return GF("\"ME\"");
      case MessageStorageType::SIMPreferred:
        return GF("\"SM_P\"");
      case MessageStorageType::PhonePreferred:
        return GF("\"ME_P\"");
      case MessageStorageType::Either_SIMPreferred:
        return GF("\"MT\"");
      }

      return GF("");
    };

    sendAT(GF("+CPMS="),
           convertMstToString(type[0]), GF(","),
           convertMstToString(type[1]), GF(","),
           convertMstToString(type[2]));

    return waitResponse() == 1;
  }

  bool deleteSmsMessage(const uint8_t index) {
    sendAT(GF("+CMGD="), index, GF(","), 0); // Delete SMS Message from <mem1> location
    return waitResponse(5000L) == 1;
  }

  bool deleteAllSmsMessages(const DeleteAllSmsMethod method) {
    // Select SMS Message Format: PDU mode. Spares us space now
    sendAT(GF("+CMGF=0"));
    if (waitResponse() != 1) {
        return false;
    }

    sendAT(GF("+CMGDA="), static_cast<const uint8_t>(method)); // Delete All SMS
    const bool ok = waitResponse(25000L) == 1;

    sendAT(GF("+CMGF=1"));
    waitResponse();

    return ok;
  }

  bool receiveNewMessageIndication(const bool enabled = true, const bool cbmIndication = false, const bool statusReport = false) {
    sendAT(GF("+CNMI=2,"),           // New SMS Message Indications
           enabled, GF(","),         // format: +CMTI: <mem>,<index>
           cbmIndication, GF(","),   // format: +CBM: <sn>,<mid>,<dcs>,<page>,<pages><CR><LF><data>
           statusReport, GF(",0"));  // format: +CDS: <fo>,<mr>[,<ra>][,<tora>],<scts>,<dt>,<st>

    return waitResponse() == 1;
  }




int getUnreadMessages(String stat=_statSMS.REC_UNREAD) {

int result; 
String MemoryStorage;
int Total1;

  sendAT(GF("+CMGF=1")); // set text mode sms
  waitResponse();

 sendAT(GF("+CPMS?"));  //,"\"",stat,"\""); Preferred SMS Message Storage

  if (waitResponse(GF(GSM_NL "+CPMS: \"")) != 1) {
      stream.readString();
      return {};
    }
// streamSkipUntil('"'); // Skip space
 MemoryStorage = stream.readStringUntil('"');
streamSkipUntil(','); // last "" of type
Total1= stream.readStringUntil(',').toInt();
DBG("Storage::>",MemoryStorage,"<");
DBG("Total1:",Total1);
  return Total1;
}




  /*
   * Phonebook functions
   */

  PhonebookStorage getPhonebookStorage() {
    sendAT(GF("+CPBS?")); // Phonebook Memory Storage
    if (waitResponse(GF(GSM_NL "+CPBS: \"")) != 1) {
      stream.readString();
      return {};
    }

    // AT reply:
    // +CPBS: <storage>,<used>,<total>

    PhonebookStorage phonebookStorage;

    const String mem = stream.readStringUntil('"');
    if (mem == GF("SM")) {
      phonebookStorage.type = PhonebookStorageType::SIM;
    } else if (mem == GF("ME")) {
      phonebookStorage.type = PhonebookStorageType::Phone;
    } else {
      stream.readString();
      return {};
    }

    // used, total
    streamSkipUntil(',');
    phonebookStorage.used = static_cast<uint8_t>(stream.readStringUntil(',').toInt());
    phonebookStorage.total = static_cast<uint8_t>(stream.readString().toInt());

    return phonebookStorage;
  }

  bool setPhonebookStorage(const PhonebookStorageType type) {
    if (type == PhonebookStorageType::Invalid) {
      return false;
    }

    const auto storage = type == PhonebookStorageType::SIM ? GF("\"SM\"") : GF("\"ME\"");
    sendAT(GF("+CPBS="), storage); // Phonebook Memory Storage

    return waitResponse() == 1;
  }

  bool addPhonebookEntry(const String &number, const String &text) {
    // Always use international phone number style (+12345678910).
    // Never use double quotes or backslashes in `text`, not even in escaped form.
    // Use characters found in the GSM alphabet.

    // Typical maximum length of `number`: 38
    // Typical maximum length of `text`:   14

    changeCharacterSet(GF("GSM"));

    // AT format:
    // AT+CPBW=<index>[,<number>,[<type>,[<text>]]]
    sendAT(GF("+CPBW=,\""), number, GF("\",145,\""), text, '"');  // Write Phonebook Entry

    return waitResponse(3000L) == 1;
  }

  bool deletePhonebookEntry(const uint8_t index) {
    // AT+CPBW=<index>
    sendAT(GF("+CPBW="), index); // Write Phonebook Entry

    // Returns OK even if an empty index is deleted in the valid range
    return waitResponse(3000L) == 1;
  }

  PhonebookEntry readPhonebookEntry(const uint8_t index) {
    changeCharacterSet(GF("GSM"));
    sendAT(GF("+CPBR="), index); // Read Current Phonebook Entries

    // AT response:
    // +CPBR:<index1>,<number>,<type>,<text>
    if (waitResponse(3000L, GF(GSM_NL "+CPBR: ")) != 1) {
      stream.readString();
      return {};
    }

    PhonebookEntry phonebookEntry;
    streamSkipUntil('"');
    phonebookEntry.number = stream.readStringUntil('"');
    streamSkipUntil(','); 
    phonebookEntry.type  = (stream.readStringUntil(',')=="145") ? F("INTERNATIONAL"):F("NATIONAL");  // Добавлен тип номера
    streamSkipUntil('"');
    phonebookEntry.text = stream.readStringUntil('"');

    waitResponse();

    return phonebookEntry;
  }

  PhonebookMatches findPhonebookEntries(const String &needle) {
    // Search among the `text` entries only.
    // Only the first TINY_GSM_PHONEBOOK_RESULTS indices are returned.
    // Make your query more specific if you have more results than that.
    // Use characters found in the GSM alphabet.

    changeCharacterSet(GF("GSM"));
    sendAT(GF("+CPBF=\""), needle, '"'); // Find Phonebook Entries

    // AT response:
    // [+CPBF:<index1>,<number>,<type>,<text>]
    // [[...]<CR><LF>+CBPF:<index2>,<number>,<type>,<text>]
    if (waitResponse(30000L, GF(GSM_NL "+CPBF: ")) != 1) {
      stream.readString();
      return {};
    }

    PhonebookMatches matches;
    for (uint8_t i = 0; i < TINY_GSM_PHONEBOOK_RESULTS; ++i) {
      matches.index[i] = static_cast<uint8_t>(stream.readStringUntil(',').toInt());
      if (waitResponse(GF(GSM_NL "+CPBF: ")) != 1) {
        break;
      }
    }

    waitResponse();

    return matches;
  }

  

  /*
   * Location functions
   */

  String getGsmLocation() {
    sendAT(GF("+CIPGSMLOC=1,1"));
    if (waitResponse(10000L, GF(GSM_NL "+CIPGSMLOC:")) != 1) {
      return "";
    }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  /*
   * Time functions
   */

  String getGSMDateTime(TinyGSMDateTimeFormat format) {
    sendAT(GF("+CCLK?"));
    if (waitResponse(2000L, GF(GSM_NL "+CCLK: \"")) != 1) {
      return "";
    }

    String res;

    switch(format) {
      case DATE_FULL:
        res = stream.readStringUntil('"');
      break;
      case DATE_TIME:
        streamSkipUntil(',');
        res = stream.readStringUntil('"');
      break;
      case DATE_DATE:
        res = stream.readStringUntil(',');
      break;
    }
    return res;
  }

  /*
   * Battery & temperature functions
   */

  // Use: float vBatt = modem.getBattVoltage() / 1000.0;
  uint16_t getBattVoltage() {
    sendAT(GF("+CBC"));
    if (waitResponse(GF(GSM_NL "+CBC:")) != 1) {
      return 0;
    }
    streamSkipUntil(','); // Skip battery charge status
    streamSkipUntil(','); // Skip battery charge level
    // return voltage in mV
    uint16_t res = stream.readStringUntil(',').toInt();
    // Wait for final OK
    waitResponse();
    return res;
  }

  int8_t getBattPercent() {
    sendAT(GF("+CBC"));
    if (waitResponse(GF(GSM_NL "+CBC:")) != 1) {
      return false;
    }
    streamSkipUntil(','); // Skip battery charge status
    // Read battery charge level
    int res = stream.readStringUntil(',').toInt();
    // Wait for final OK
    waitResponse();
    return res;
  }

  uint8_t getBattChargeState() {
    sendAT(GF("+CBC?"));
    if (waitResponse(GF(GSM_NL "+CBC:")) != 1) {
      return false;
    }
    // Read battery charge status
    int res = stream.readStringUntil(',').toInt();
    // Wait for final OK
    waitResponse();
    return res;
  }

  bool getBattStats(uint8_t &chargeState, int8_t &percent, uint16_t &milliVolts) {
    sendAT(GF("+CBC?"));
    if (waitResponse(GF(GSM_NL "+CBC:")) != 1) {
      return false;
    }
    chargeState = stream.readStringUntil(',').toInt();
    percent = stream.readStringUntil(',').toInt();
    milliVolts = stream.readStringUntil('\n').toInt();
    // Wait for final OK
    waitResponse();
    return true;
  }

  float getTemperature(){
  return 36.6F;

  } //TINY_GSM_ATTR_NOT_AVAILABLE;

  /*
   * NTP server functions
   */

  boolean isValidNumber(String str) {
    if(!(str.charAt(0) == '+' || str.charAt(0) == '-' || isDigit(str.charAt(0)))) return false;

    for(byte i=1;i < str.length(); i++) {
      if(!(isDigit(str.charAt(i)) || str.charAt(i) == '.')) return false;
    }
    return true;
  }

  String ShowNTPError(byte error) {
    switch (error) {
      case 1:
        return "Network time synchronization is successful";
      case 61:
        return "Network error";
      case 62:
        return "DNS resolution error";
      case 63:
        return "Connection error";
      case 64:
        return "Service response error";
      case 65:
        return "Service response timeout";
      default:
        return "Unknown error: " + String(error);
    }
  }

  byte NTPServerSync(String server = "pool.ntp.org", byte TimeZone = 3) {
    sendAT(GF("+CNTPCID=1"));
    if (waitResponse(10000L) != 1) {
        return -1;
    }

    sendAT(GF("+CNTP="), server, ',', String(TimeZone));
    if (waitResponse(10000L) != 1) {
        return -1;
    }

    sendAT(GF("+CNTP"));
    if (waitResponse(10000L, GF(GSM_NL "+CNTP:"))) {
        String result = stream.readStringUntil('\n');
        result.trim();
        if (isValidNumber(result))
        {
          return result.toInt();
        }
    }
    else {
      return -1;
    }
    return -1;
  }

  /*
   * Client related functions
   */

protected:

  bool modemConnect(const char* host, uint16_t port, uint8_t mux,
                    bool ssl = false, int timeout_s = 75)
 {
    int rsp;
    uint32_t timeout_ms = ((uint32_t)timeout_s)*1000;
#if !defined(TINY_GSM_MODEM_SIM900)
    sendAT(GF("+CIPSSL="), ssl);
    rsp = waitResponse();
    if (ssl && rsp != 1) {
      return false;
    }
#endif
    sendAT(GF("+CIPSTART="), mux, ',', GF("\"TCP"), GF("\",\""), host, GF("\","), port);
    rsp = waitResponse(timeout_ms,
                       GF("CONNECT OK" GSM_NL),
                       GF("CONNECT FAIL" GSM_NL),
                       GF("ALREADY CONNECT" GSM_NL),
                       GF("ERROR" GSM_NL),
                       GF("CLOSE OK" GSM_NL)   // Happens when HTTPS handshake fails
                      );
    return (1 == rsp);
  }

  int16_t modemSend(const void* buff, size_t len, uint8_t mux) {
    sendAT(GF("+CIPSEND="), mux, ',', (uint16_t)len);
    if (waitResponse(GF(">")) != 1) {
      return 0;
    }
    stream.write((uint8_t*)buff, len);
    stream.flush();
    if (waitResponse(GF(GSM_NL "DATA ACCEPT:")) != 1) {
      return 0;
    }
    streamSkipUntil(','); // Skip mux
    return stream.readStringUntil('\n').toInt();
  }

  size_t modemRead(size_t size, uint8_t mux) {
#ifdef TINY_GSM_USE_HEX
    sendAT(GF("+CIPRXGET=3,"), mux, ',', (uint16_t)size);
    if (waitResponse(GF("+CIPRXGET:")) != 1) {
      return 0;
    }
#else
    sendAT(GF("+CIPRXGET=2,"), mux, ',', (uint16_t)size);
    if (waitResponse(GF("+CIPRXGET:")) != 1) {
      return 0;
    }
#endif
    streamSkipUntil(','); // Skip Rx mode 2/normal or 3/HEX
    streamSkipUntil(','); // Skip mux
    int len_requested = stream.readStringUntil(',').toInt();
    //  ^^ Requested number of data bytes (1-1460 bytes)to be read
    int len_confirmed = stream.readStringUntil('\n').toInt();
    // ^^ Confirmed number of data bytes to be read, which may be less than requested.
    // 0 indicates that no data can be read.
    // This is actually be the number of bytes that will be remaining after the read
    for (int i=0; i<len_requested; i++) {
      uint32_t startMillis = millis();
#ifdef TINY_GSM_USE_HEX
      while (stream.available() < 2 && (millis() - startMillis < sockets[mux]->_timeout)) { TINY_GSM_YIELD(); }
      char buf[4] = { 0, };
      buf[0] = stream.read();
      buf[1] = stream.read();
      char c = strtol(buf, NULL, 16);
#else
      while (!stream.available() && (millis() - startMillis < sockets[mux]->_timeout)) { TINY_GSM_YIELD(); }
      char c = stream.read();
#endif
      sockets[mux]->rx.put(c);
    }
    DBG("### READ:", len_requested, "from", mux);
    // sockets[mux]->sock_available = modemGetAvailable(mux);
    sockets[mux]->sock_available = len_confirmed;
    waitResponse();
    return len_requested;
  }

  size_t modemGetAvailable(uint8_t mux) {
    sendAT(GF("+CIPRXGET=4,"), mux);
    size_t result = 0;
    if (waitResponse(GF("+CIPRXGET:")) == 1) {
      streamSkipUntil(','); // Skip mode 4
      streamSkipUntil(','); // Skip mux
      result = stream.readStringUntil('\n').toInt();
      waitResponse();
    }
    DBG("### Available:", result, "on", mux);
    if (!result) {
      sockets[mux]->sock_connected = modemGetConnected(mux);
    }
    return result;
  }

  bool modemGetConnected(uint8_t mux) {
    sendAT(GF("+CIPSTATUS="), mux);
    waitResponse(GF("+CIPSTATUS"));
    int res = waitResponse(GF(",\"CONNECTED\""), GF(",\"CLOSED\""), GF(",\"CLOSING\""),
                           GF(",\"REMOTE CLOSING\""), GF(",\"INITIAL\""));
    waitResponse();
    return 1 == res;
  }

public:

  /*
   Utilities
   */

TINY_GSM_MODEM_STREAM_UTILITIES()

  // TODO: Optimize this!
  uint8_t waitResponse(uint32_t timeout_ms, String& data,
                       GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    /*String r1s(r1); r1s.trim();
    String r2s(r2); r2s.trim();
    String r3s(r3); r3s.trim();
    String r4s(r4); r4s.trim();
    String r5s(r5); r5s.trim();
    DBG("### ..:", r1s, ",", r2s, ",", r3s, ",", r4s, ",", r5s);*/
    data.reserve(64);
    int index = 0;
    unsigned long startMillis = millis();
    do {
      TINY_GSM_YIELD();
      while (stream.available() > 0) {
        int a = stream.read();
        if (a <= 0) continue; // Skip 0x00 bytes, just in case
        data += (char)a;
        if (r1 && data.endsWith(r1)) {
          index = 1;
          goto finish;
        } else if (r2 && data.endsWith(r2)) {
          index = 2;
          goto finish;
        } else if (r3 && data.endsWith(r3)) {
          index = 3;
          goto finish;
        } else if (r4 && data.endsWith(r4)) {
          index = 4;
          goto finish;
        } else if (r5 && data.endsWith(r5)) {
          index = 5;
          goto finish;
        } else if (data.endsWith(GF(GSM_NL "+CIPRXGET:"))) {
          String mode = stream.readStringUntil(',');
          if (mode.toInt() == 1) {
            int mux = stream.readStringUntil('\n').toInt();
            if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
              sockets[mux]->got_data = true;
            }
            data = "";
            DBG("### Got Data:", mux);
          } else {
            data += mode;
          }
        } else if (data.endsWith(GF(GSM_NL "+RECEIVE:"))) {
          int mux = stream.readStringUntil(',').toInt();
          int len = stream.readStringUntil('\n').toInt();
          if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
            sockets[mux]->got_data = true;
            sockets[mux]->sock_available = len;
          }
          data = "";
          DBG("### Got Data:", len, "on", mux);
        } else if (data.endsWith(GF("CLOSED" GSM_NL))) {
          int nl = data.lastIndexOf(GSM_NL, data.length()-8);
          int coma = data.indexOf(',', nl+2);
          int mux = data.substring(nl+2, coma).toInt();
          if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
            sockets[mux]->sock_connected = false;
          }
          data = "";
          DBG("### Closed: ", mux);
        }
      }
    } while (millis() - startMillis < timeout_ms);
finish:
    if (!index) {
      data.trim();

      if (data.length()) {
        DBG("### Unhandled: ", data,"<");
      }

      data = "";
    }

//    data.replace(GSM_NL, "/");
//    DBG('<', index, '>', data);

    return index;
  }

  uint8_t waitResponse(uint32_t timeout_ms,
                       GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    String data;
    return waitResponse(timeout_ms, data, r1, r2, r3, r4, r5);
  }

  uint8_t waitResponse(GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    return waitResponse(1000, r1, r2, r3, r4, r5);
  }

public:
  Stream&       stream;

protected:
  GsmClient*    sockets[TINY_GSM_MUX_COUNT];

  bool changeCharacterSet(const String &alphabet) {
    sendAT(GF("+CSCS=\""), alphabet, '"');
    return waitResponse() == 1;
  }

};


#endif
