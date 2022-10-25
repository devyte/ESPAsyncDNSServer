Oct 25 2022 Olaf Kolkman

Fix: reply with malformed SERVFAIL packets, i.e. with an additional couple of bytes in the packet. Now: clean DNS packet boundary.
Fix: code would reply with A record also for other QTYPES. Now: will answer only A type queries, 
Fix: code would return SERVAIL when EDNS was set on request (and copy all its content as extraneous packet content). Now: cleanly ignores EDNS0
Fix: Added check on parsing of QNAME to prevent potential problems with corrupt QNAMES