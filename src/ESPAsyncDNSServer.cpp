#include "ESPAsyncDNSServer.h"
#include <lwip/def.h>
#include <Arduino.h>

#define SIZECLASS 2
#define SIZETYPE 2
#define DATALENGTH 4

namespace
{


  struct DNSHeader
  {
  uint16_t ID;               // identification number
  unsigned char RD : 1;      // recursion desired
  unsigned char TC : 1;      // truncated message
  unsigned char AA : 1;      // authoritive answer
  unsigned char OPCode : 4;  // message_type
  unsigned char QR : 1;      // query/response flag
  unsigned char RCode : 4;   // response code
  unsigned char Z : 3;       // its z! reserved
  unsigned char RA : 1;      // recursion available
  uint16_t QDCount;          // number of question entries
  uint16_t ANCount;          // number of answer entries
  uint16_t NSCount;          // number of authority entries
  uint16_t ARCount;          // number of resource entries
  };

  


bool
  requestIncludesOnlyOneAQuestion(AsyncUDPPacket &packet, size_t _qnameLength)
  {
    unsigned char *_buffer = packet.data();
    DNSHeader *_dnsHeader = (DNSHeader *)_buffer;
    unsigned char *_startQname = _buffer + sizeof(DNSHeader);  
    if (ntohs(_dnsHeader->QDCount) == 1 &&
        _dnsHeader->ANCount == 0 &&
        _dnsHeader->NSCount == 0)
    {
      // Test if we are dealing with a QTYPE== A
      u_int16_t qtype = *(_startQname+_qnameLength+1); // we need to skip the closing label length
      if (qtype != 0x0001 ){ // Not an A type query
        return false;
      }
      if (_dnsHeader->ARCount == 0)
      {
        return true;
      }
      else if (ntohs(_dnsHeader->ARCount) == 1)
      {
        // test if the Additional Section RR is of type EDNS
        unsigned char * _startADSection=_startQname+_qnameLength+4; //skipping the TYPE AND CLASS values of the Query Section
        // The EDNS pack must have a 0 lentght domain name followed by type 41
        if (*_startADSection != 0) //protocol violation for OPT record
        {
          return false; 
        } 
        _startADSection++;
        uint16_t *dnsType = (uint16_t *)_startADSection;
        if (ntohs(*dnsType) != 41) // something else than OPT/EDNS lives in the Additional section
        {
          return false;
        }
        return true;
      } else 
      { // AR Count != 0 or 1
        return false;
      }
    } else { // QDcount != 1 || ANcount !=0 || NSCount !=0
      return false;
    }
  }

void 
downcaseAndRemoveWwwPrefix(String &domainName)
  {
    domainName.toLowerCase();
    domainName.replace("www.", "");
  }

// Declare in order to overload
String
      getDomainNameWithoutWwwPrefix(unsigned char *);
String
      getDomainNameWithoutWwwPrefix(unsigned char *, size_t &);


String
      getDomainNameWithoutWwwPrefix(unsigned char *start){
        size_t qnameLentghDummy;
        return(getDomainNameWithoutWwwPrefix(start,qnameLentghDummy));
      }

  String // will set the length of the qname section in qNameLength.
      getDomainNameWithoutWwwPrefix(unsigned char *start, size_t & _qnameLength)
  {
  String parsedDomainName = "";
    if (start == nullptr || *start == 0){
      _qnameLength=0;
      return parsedDomainName;
    }
    int pos = 0;
    while(true)
    {
      unsigned char labelLength = *(start + pos);
    for(int i = 0; i < labelLength; i++)
      {
        pos++;
        parsedDomainName += (char)*(start + pos);
      }
      pos++;
      if (pos>254){
        // failsafe, A DNAME may not be longer than 255 octets RFC1035 3.1
        _qnameLength=1; // DNAME is a zero length byte
        return "";
      }
      if (*(start + pos) == 0)
      {
        _qnameLength =(size_t)(pos)+1;  // We need to add the clossing label to the length
        downcaseAndRemoveWwwPrefix(parsedDomainName);
        
        return parsedDomainName;
      }
      else
      {
        parsedDomainName += ".";
      }
    }
  }



} // namespace








AsyncDNSServer::AsyncDNSServer()
  {
    _ttl = htonl(60);
    _errorReplyCode = AsyncDNSReplyCode::NonExistentDomain;
  }

bool 
AsyncDNSServer::start(const uint16_t port, const String &domainName,
                             const IPAddress &resolvedIP)
  {
    _port = port;
    _domainName = domainName;
    _resolvedIP[0] = resolvedIP[0];
    _resolvedIP[1] = resolvedIP[1];
    _resolvedIP[2] = resolvedIP[2];
    _resolvedIP[3] = resolvedIP[3];
    downcaseAndRemoveWwwPrefix(_domainName);
  if(_udp.listen(_port))
    {
      _udp.onPacket(
          [&](AsyncUDPPacket &packet)
          {
            this->processRequest(packet);
      }
    );
      return true;
    }
    return false;
  }

void 
AsyncDNSServer::setErrorReplyCode(const AsyncDNSReplyCode &replyCode)
  {
    _errorReplyCode = replyCode;
  }

void 
AsyncDNSServer::setTTL(const uint32_t ttl)
  {
    _ttl = htonl(ttl);
  }

void 
AsyncDNSServer::stop()
  {
    _udp.close();
  }

void 
AsyncDNSServer::processRequest(AsyncUDPPacket &packet)
  {
    if (packet.length() >= sizeof(DNSHeader))
    {
    
      unsigned char *_buffer = packet.data();
      DNSHeader *_dnsHeader = (DNSHeader *)_buffer;
      size_t qnameLength=0;
      String domainNameWithoutWwwPrefix = (_buffer == nullptr ? "" : getDomainNameWithoutWwwPrefix(_buffer + sizeof(DNSHeader), qnameLength));

      if (_dnsHeader->QR == DNS_QR_QUERY &&
          _dnsHeader->OPCode == DNS_OPCODE_QUERY &&
          requestIncludesOnlyOneAQuestion(packet,qnameLength) &&   // proxy for requestIncludesOnlyAOneQuestion(packet) 
          (_domainName == "*" || domainNameWithoutWwwPrefix == _domainName))
      {
        replyWithIP(packet,qnameLength);
      }
      else if (_dnsHeader->QR == DNS_QR_QUERY)
      {
        replyWithCustomCode(packet,qnameLength);
      }
    }
  }



void 
AsyncDNSServer::replyWithIP(AsyncUDPPacket &packet, size_t &_qnameLength)
  {

  // DNS Header + qname + Type +  Class + qnamePointer  + TYPE + CLASS + TTL + Datalength ) IP 
  // sizeof(DNSHeader) + _qnameLength  + 2*SIZECLASS +2*SIZETYPE + sizeof(_ttl) + DATLENTHG + sizeof(_resolvedIP)
  AsyncUDPMessage msg(sizeof(DNSHeader) + _qnameLength +  2*SIZECLASS +2*SIZETYPE +sizeof(_ttl) + DATALENGTH+ sizeof(_resolvedIP)); 

    msg.write(packet.data(), sizeof(DNSHeader)+_qnameLength + 4); // Question Section included.
    DNSHeader * _dnsHeader = (DNSHeader *)msg.data();

    _dnsHeader->QR = DNS_QR_RESPONSE;
    _dnsHeader->ANCount = htons(1); 
    _dnsHeader->QDCount = _dnsHeader->QDCount;
    _dnsHeader->ARCount = 0;

    //_dnsHeader->RA = 1;

  msg.write((uint8_t)192); //  answer name is a pointer
  msg.write((uint8_t)12);  // pointer to offset at 0x00c

    msg.write((uint8_t)0);   // 0x0001  answer is type A query (host address)
    msg.write((uint8_t)1);

    msg.write((uint8_t)0);   //0x0001 answer is class IN (internet address)
    msg.write((uint8_t)1);

    msg.write((uint8_t *)&_ttl, sizeof(_ttl));

    // Length of RData is 4 bytes (because, in this case, RData is IPv4)
    msg.write((uint8_t)0);
    msg.write((uint8_t)4);
    msg.write(_resolvedIP, sizeof(_resolvedIP));

    packet.send(msg);


#ifdef DEBUG
    DEBUG_OUTPUT.print("DNS responds: ");
    DEBUG_OUTPUT.print(_resolvedIP[0]);
    DEBUG_OUTPUT.print(".");
    DEBUG_OUTPUT.print(_resolvedIP[1]);
    DEBUG_OUTPUT.print(".");
    DEBUG_OUTPUT.print(_resolvedIP[2]);
    DEBUG_OUTPUT.print(".");
    DEBUG_OUTPUT.print(_resolvedIP[3]);
    //    DEBUG_OUTPUT.print(" for ");
    //    DEBUG_OUTPUT.println(getDomainNameWithoutWwwPrefix());
#endif
  }

void
AsyncDNSServer::replyWithCustomCode(AsyncUDPPacket &packet, size_t &_qnameLength)
  {

  AsyncUDPMessage msg(sizeof(DNSHeader)); 

    msg.write(packet.data(), sizeof(DNSHeader)); // Question Section included.
    DNSHeader * _dnsHeader = (DNSHeader *)msg.data();

    _dnsHeader->QR = DNS_QR_RESPONSE;
  _dnsHeader->RCode = (unsigned char)_errorReplyCode; //default is AsyncDNSReplyCode::NonExistentDomain
  _dnsHeader->QDCount = 0;
  _dnsHeader->ARCount = 0;

      packet.send(msg);
}
  