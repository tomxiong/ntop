/*
 *  Copyright (C) 1998-2000 Luca Deri <deri@ntop.org>
 *                          Portions by Stefano Suin <stefano@ntop.org>
 *
 *		  	  Centro SERRA, University of Pisa
 *		 	  http://www.ntop.org/
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * Copyright (c) 1994, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "ntop.h"


#ifdef DEBUG
static int numChildren=0;
#endif

static int enableDBsupport=0;

/* *************************** */

#ifndef WIN32
void handleSigHup(int signalId _UNUSED_) {
  traceEvent(TRACE_INFO, "Caught sighup: statistics have been reset.\n");
  resetStats();
  (void)setsignal(SIGHUP,  handleSigHup);
}

#endif /* WIN32 */

/* *************************** */

#ifdef MULTITHREADED
void* pcapDispatch(void *_i) {
  int rc;
  int i = (int)_i;

  for(;capturePackets == 1;) {
    rc = pcap_dispatch(device[i].pcapPtr, -1, queuePacket, (u_char*) &_i);
    if(rc == -1) {
      traceEvent(TRACE_ERROR, "Error while reading packets: %s.\n",
		 pcap_geterr(device[i].pcapPtr));
      break;
    } /* elsetraceEvent(TRACE_INFO, "1) %d\n", numPkts++); */
  }

  return(NULL);
}
#endif

/* **************************************** */

#ifndef WIN32
RETSIGTYPE handleDiedChild(int signal _UNUSED_) {
  int status;
  pid_t pidId;

  pidId = waitpid(-1, &status, WNOHANG);

#ifdef DEBUG
  if(status == 0) {
    numChildren--;
    traceEvent(TRACE_INFO,
	       "A child has terminated [pid=%d status=%d children=%d]\n",
	       pidId, status, numChildren);
  }
#endif

  (void)setsignal(SIGCHLD, handleDiedChild);
}
#endif


/* **************************************** */

RETSIGTYPE dontFreeze(int signo _UNUSED_) {
#ifdef DEBUG
  traceEvent(TRACE_INFO, "Caught a SIGALRM...\n");
#endif
#ifndef WIN32
  (void)setsignal(SIGALRM, dontFreeze);
#endif
}

/* **************************************** */

#ifndef WIN32

void daemonize(void) {
  int childpid;

  (void)signal(SIGHUP, SIG_IGN);
  (void)signal(SIGCHLD, SIG_IGN);
  (void)signal(SIGQUIT, SIG_IGN);

  if((childpid=fork()) < 0)
    traceEvent(TRACE_ERROR, "An error occurred while daemonizing ntop...\n");
  else {
    if(!childpid) { /* child */
      traceEvent(TRACE_INFO, "Bye bye: I'm becoming a daemon...\n");
      detachFromTerminal();
    } else { /* father */
      exit(0);
    }
  }
}

/* **************************************** */

void detachFromTerminal(void) {
#ifndef MULTITHREADED
  alarm(120); /* Don't freeze */
#endif

  setsid();  /* detach from the terminal */

#ifdef ORIGINAL_DETACH
  if (freopen("/dev/null", "r", stdin) == NULL) {
    traceEvent(TRACE_ERROR,
	       "ntop: unable to replace stdin with /dev/null: %s\n",
	       strerror(errno));
  }

  if (freopen("/dev/null", "w", stdout) == NULL) {
    traceEvent(TRACE_ERROR,
	       "ntop: unable to replace stdout with /dev/null: %s\n",
	       strerror(errno));
  }

  /*
    if (freopen("/dev/null", "w", stderr) == NULL) {
    traceEvent(TRACE_ERROR,
    "ntop: unable to replace stderr with /dev/null: %s\n",
    strerror(errno));
    }
  */
#else /* !ORIGINAL_DETACH */
  fclose(stdin);
  fclose(stdout);
  /* fclose(stderr); */

  /*
   * clear any inherited file mode creation mask
   */
  umask (0);

  /*
   * Use linebuffered stdout
   */
  /* setlinebuf (stdout); */
  setvbuf(stdout, (char *)NULL, _IOLBF, 0);

#endif /* ORIGINAL_DETACH */
}
#endif /* WIN32 */

/* **************************************** */

static short handleProtocol(char* protoName, char *protocol) {
  int i, idx, lowProtoPort, highProtoPort;
  short printWarnings = 0;

  if(protocol[0] == '\0')
    return(-1);
  else if(isdigit(protocol[0])) {
    /* numeric protocol port handling */
    lowProtoPort = highProtoPort = 0;
    sscanf(protocol, "%d-%d", &lowProtoPort, &highProtoPort);
    if(highProtoPort < lowProtoPort)
      highProtoPort = lowProtoPort;

    if(lowProtoPort < 0) lowProtoPort = 0;
    if(highProtoPort >= TOP_IP_PORT) highProtoPort = TOP_IP_PORT-1;

    for(idx=lowProtoPort; idx<= highProtoPort; idx++) {
      if(ipPortMapper[idx] == -1) {
	numIpPortsToHandle++;

#ifdef DEBUG
	printf("[%d] '%s' [port=%d]\n", numIpProtosToMonitor, protoName, idx);
#endif
	ipPortMapper[idx] = numIpProtosToMonitor;
      } else if(printWarnings)
	printf("WARNING: IP port %d (%s) has been discarded (multiple instances).\n",
	       idx, protoName);
    }

    return(1);
  }

  for(i=1; i<SERVICE_HASH_SIZE; i++) {
    idx = -1;

    if((udpSvc[i] != NULL) && (strcmp(udpSvc[i]->name, protocol) == 0))
      idx = udpSvc[i]->port;
    else if((tcpSvc[i] != NULL) && (strcmp(tcpSvc[i]->name, protocol) == 0))
      idx = tcpSvc[i]->port;

    if(idx != -1) {
      if(ipPortMapper[idx] == -1) {
	numIpPortsToHandle++;

#ifdef DEBUG
	printf("[%d] '%s' [%s:%d]\n", numIpProtosToMonitor, protoName, protocol, idx);
#endif
	ipPortMapper[idx] = numIpProtosToMonitor;
      } else if(printWarnings)
	printf("WARNING: protocol '%s' has been discarded (multiple instances).\n",
	       protocol);
      return(1);
    }
  }

  if(printWarnings)
    traceEvent(TRACE_WARNING, "WARNING: unknown protocol '%s'. It has been ignored.\n",
	       protocol);

  return(-1);
}

/* **************************************** */

static void handleProtocolList(char* protoName, 
			       char *protocolList) {
  char tmpStr[255];
  char* lastEntry, *protoEntry;
  int increment=0, rc;

#ifdef DEBUG
  traceEvent(TRACE_INFO, "%s - %s\n", protoName, protocolList);
#endif

  if(numIpProtosToMonitor == MAX_NUM_HANDLED_IP_PROTOCOLS) {
    /* MAX_NUM_HANDLED_IP_PROTOCOLS is defined in ntop.h: increase it! */
    traceEvent(TRACE_WARNING, "WARNING: Unable to handle '%s'. "
	       "You've reached the max number\n"
	       "of handled IP protocols.\n", protoName);
    return;
  }

  /* The trick below is used to avoid to modify static
     memory like in the case where this function is
     called by addDefaultProtocols()
  */
  lastEntry = strncpy(tmpStr, protocolList, sizeof(tmpStr));

  while((protoEntry  = strchr(lastEntry, '|')) != NULL) {
    protoEntry[0] = '\0';
    rc = handleProtocol(protoName, lastEntry);

    if(rc != -1)
      increment=1;

    lastEntry = &protoEntry[1];
  }

  if(increment == 1) {
    protoIPTrafficInfos[numIpProtosToMonitor] = strdup(protoName);
    numIpProtosToMonitor++;
#ifdef DEBUG
    traceEvent(TRACE_INFO, "%d) %s - %s\n",
	       numIpProtosToMonitor, protoName, protocolList);
#endif
  }
}

/* **************************************** */

void handleProtocols(char *protos) {
  char *proto, *buffer=NULL, *strtokState;
  FILE *fd = fopen(protos, "rb");

  if(fd == NULL)
    proto = strtok_r(protos, ",", &strtokState);
  else {
    struct stat buf;
    int len, i;

    if(stat(protos, &buf) != 0) {
      traceEvent(TRACE_ERROR, "Error while stat() of %s\n", protos);
      return;
    }

    buffer = (char*)malloc(buf.st_size+8) /* just to be safe */;

    for(i=0;i<buf.st_size;) {
      len = fread(&buffer[i], sizeof(char), buf.st_size-i, fd);
      if(len <= 0) break;
      i += len;
    }

    fclose(fd);

    /* remove trailing carriage return */
    if(buffer[strlen(buffer)-1] == '\n')
      buffer[strlen(buffer)-1] = 0;

    proto = strtok_r(buffer, ",", &strtokState);
  }

  while(proto != NULL) {
    char* protoName = strchr(proto, '=');

    if(protoName == NULL)
      traceEvent(TRACE_INFO,
		 "Unknown protocol '%s'. It has been ignored.\n",
		 proto);
    else {
      char tmpStr[255];
      int len;

      protoName[0] = '\0';
      memset(tmpStr, 0, sizeof(tmpStr));
      strncpy(tmpStr, &protoName[1], sizeof(tmpStr));
      len = strlen(tmpStr);

      if(tmpStr[len-1] != '|') {
	/* Make sure that the string ends with '|' */
	tmpStr[len] = '|';
	tmpStr[len+1] = '\0';
      }

      handleProtocolList(proto, tmpStr);

    }
    proto = strtok_r(NULL, ",", &strtokState);
  }

  if(buffer !=NULL)
    free(buffer);
}

/* **************************************** */

void addDefaultProtocols(void) {
  handleProtocolList("FTP", "ftp|ftp-data|");
  handleProtocolList("HTTP", "http|www|https|");
  handleProtocolList("DNS", "name|domain|");
  handleProtocolList("Telnet", "telnet|login|");
  handleProtocolList("NBios-IP", "netbios-ns|netbios-dgm|netbios-ssn|");
  handleProtocolList("Mail", "pop-2|pop-3|pop3|kpop|smtp|imap|imap2|");
  handleProtocolList("SNMP", "snmp|snmp-trap|");
  handleProtocolList("NEWS", "nntp|");
  handleProtocolList("NFS", "mount|pcnfs|bwnfs|nfsd|nfsd-status|");
  handleProtocolList("X11", "6000-6010|");
  /* 22 == ssh (just to make sure the port is defined) */
  handleProtocolList("SSH", "22|"); 
}

/* **************************************** */

int mapGlobalToLocalIdx(int port) {
  if((port < 0) || (port >= TOP_IP_PORT))
   return -1;
  else {
#ifdef DEBUG
    traceEvent(TRACE_INFO, "IP port %d -> %d\n",
	       port, ipPortMapper[port]);
#endif
    return(ipPortMapper[port]);
  }
}

/* **************************************** */

#ifdef MULTITHREADED
void* updateThptLoop(void* notUsed _UNUSED_) {
  for(;;) {
#ifdef DEBUG
    traceEvent(TRACE_INFO, "Sleeping for %d seconds\n",
	       THROUGHPUT_REFRESH_TIME);
#endif

    sleep(THROUGHPUT_REFRESH_TIME);

    if(!capturePackets) break;

#ifdef DEBUG
    traceEvent(TRACE_INFO, "Trying to update throughput\n");
#endif

    /* Don't update Thpt if the traffic is high */
    /* if(packetQueueLen < (PACKET_QUEUE_LENGTH/3)) */ {
      actTime = time(NULL);
#ifdef MULTITHREADED
      accessMutex(&hostsHashMutex,"updateThptLoop");
#endif
#ifdef DEBUG
      traceEvent(TRACE_INFO, "Updating throughput\n");
#endif
      updateThpt(); /* Update Throughput */
#ifdef MULTITHREADED
      releaseMutex(&hostsHashMutex);
#endif
    }
  }
  return(NULL);

}

/* **************************************** */

#ifdef MULTITHREADED
void* updateDBHostsTrafficLoop(void* notUsed _UNUSED_) {
  u_short updateTime = DEFAULT_DB_UPDATE_TIME; /* This should be user configurable */

  for(;;) {
#ifdef DEBUG
    traceEvent(TRACE_INFO, "Sleeping for %d seconds\n", updateTime);
#endif

    sleep(updateTime);

    if(!capturePackets) break;

    /* accessMutex(&hostsHashMutex); */
    /* CHECK ME: Parmeter to updateDbHostsTraffic */ 
    updateDbHostsTraffic(0);
    /* releaseMutex(&hostsHashMutex); */
  }
  return(NULL);

}
#endif

/* **************************************** */

void* scanIdleLoop(void* notUsed _UNUSED_) {
  for(;;) {
    sleep(SESSION_SCAN_DELAY);

    if(!capturePackets) break;
    actTime = time(NULL);

    /* Don't purge hosts if the traffic is high */
    /* if(packetQueueLen < (PACKET_QUEUE_LENGTH/3)) */ {
#ifdef MULTITHREADED
      accessMutex(&hostsHashMutex, "scanIdleLoop");
#endif
      scanTimedoutTCPSessions();
#ifdef MULTITHREADED
      releaseMutex(&hostsHashMutex);
#endif

      sleep(1); /* Give some time to others... */

#ifdef MULTITHREADED
      accessMutex(&hostsHashMutex, "scanIdleLoop");
#endif
      purgeOldFragmentEntries();
#ifdef MULTITHREADED
      releaseMutex(&hostsHashMutex);
#endif

      sleep(1); /* Give some time to others... */

#ifdef MULTITHREADED
      accessMutex(&hostsHashMutex, "scanIdleLoop");
#endif
      purgeIdleHosts(0 /* Delete only idle hosts */);
#ifdef MULTITHREADED
      releaseMutex(&hostsHashMutex);
#endif
    }

    if(handleRules)
      scanAllTcpExpiredRules();
  }

  return(NULL);
}

/* **************************************** */

#ifdef MULTITHREADED
void* periodicLsofLoop(void* notUsed _UNUSED_) {
  for(;;) {
    /*
      refresh process list each minute
      if needed
    */

    if(!capturePackets) break;

    if(updateLsof) {
#ifdef DEBUG
      traceEvent(TRACE_INFO, "Wait please: reading lsof information...\n");
#endif
      if(isLsofPresent) readLsofInfo();
      if(isNepedPresent) readNepedInfo();
#ifdef DEBUG
      traceEvent(TRACE_INFO, "Done with lsof.\n");
#endif
    }
    sleep(60);
  }
  return(NULL);

}
#endif

#endif

/* **************************************** */

#ifndef MULTITHREADED
void packetCaptureLoop(time_t *lastTime, int refreshRate) {
  int numPkts=0;

  for(;;) {
    short justRefreshed;
    int rc;

    if(!capturePackets) break;

    rc = pcap_dispatch(device[0].pcapPtr, -1, processPacket, NULL);

    if(rc == -1) {
      traceEvent(TRACE_ERROR, "Error while reading packets: %s.\n",
		 pcap_geterr(device[0].pcapPtr));
      continue;
    }

    actTime = time(NULL);

    if(actTime > (*lastTime)) {
      if(nextSessionTimeoutScan < actTime) {
	/* It's time to check for timeout sessions */
	scanTimedoutTCPSessions();
	nextSessionTimeoutScan = actTime+SESSION_SCAN_DELAY;
      }

      if(handleRules)
	scanAllTcpExpiredRules();
      updateThpt(); /* Update Throughput */
      (*lastTime) = actTime + THROUGHPUT_REFRESH_TIME;
      justRefreshed=1;
    } else
      justRefreshed=0;

    handleWebConnections(NULL);

    if((logTimeout != 0) && (actTime > nextLogTime)) {
      LogStatsToFile();
      nextLogTime = actTime + logTimeout;
    }
  } /* for(;;) */
}
#endif

/* **************************************** */

/* Report statistics and write out the raw packet file */
RETSIGTYPE cleanup(int signo) {
  static int unloaded = 0;
  struct pcap_stat stat;
  int i;

  /* traceEvent(TRACE_INFO, "Cleanup called.\n"); */

  if(unloaded)
    return;
  else
    unloaded = 1;

  capturePackets = 0;

  /* traceEvent(TRACE_INFO, "==> capturePackets: %d\n", capturePackets); */

#ifdef MULTITHREADED
  /* Courtesy of Felipe Tonioli <tonioli@mtec.com.br> */
  if(signo != -1) { /* the user pressed the 'q' key */
    killThread(&dequeueThreadId);
    killThread(&thptUpdateThreadId);
    killThread(&scanIdleThreadId);
    if(enableDBsupport)
      killThread(&dbUpdateThreadId);

    if(isLsofPresent)
      killThread(&lsofThreadId);

#ifdef ASYNC_ADDRESS_RESOLUTION
    if(numericFlag == 0)
      killThread(&dequeueAddressThreadId);
#endif

    killThread(&handleWebConnectionsThreadId);

    deleteMutex(&packetQueueMutex);
    deleteMutex(&addressResolutionMutex);
    deleteMutex(&hashResizeMutex);
    deleteMutex(&hostsHashMutex);
    deleteMutex(&graphMutex);
    if(isLsofPresent)
      deleteMutex(&lsofMutex);
#ifdef USE_SEMAPHORES
    deleteSem(&queueSem);
#ifdef ASYNC_ADDRESS_RESOLUTION
    deleteSem(&queueAddressSem);
#endif
#else
    deleteCondvar(&queueCondvar);
#ifdef ASYNC_ADDRESS_RESOLUTION
    signalCondvar(&queueAddressCondvar);
    deleteCondvar(&queueAddressCondvar);
#endif
#endif
  }

#ifdef FULL_MEMORY_FREE
  cleanupAddressQueue();
  cleanupPacketQueue();
#endif
#endif

#ifdef FULL_MEMORY_FREE
  freeHostInstances();
#endif
  unloadPlugins();
  termLogger();
  (void)fflush(stdout);

  termIPServices();
  termIPSessions();

  if(logTimeout) fclose(logd);

#ifndef WIN32
  endservent();
#endif

#ifdef HAVE_GDBM_H
  gdbm_close(gdbm_file);
  gdbm_close(pwFile);
  if(eventFile != NULL) gdbm_close(eventFile);
#ifdef MULTITHREADED
  deleteMutex(&gdbmMutex);
#endif
#endif

  if(rFileName == NULL)
    for(i=0; i<numDevices; i++) {
      if (pcap_stats(device[i].pcapPtr, &stat) < 0) {
	/*traceEvent(TRACE_INFO, "\n\npcap_stats: %s\n",
	  pcap_geterr(device[i].pcapPtr)); */
      } else {
	printf("%s packets received by filter on %s\n",
	       formatPkts((TrafficCounter)stat.ps_recv), device[i].name);
	printf("%s packets dropped by kernel\n",
	       formatPkts((TrafficCounter)(stat.ps_drop)));
#ifdef MULTITHREADED
	printf("%s packets dropped by ntop\n",
	       formatPkts(device[i].droppedPackets));
#endif
      }
    }

  if(enableDBsupport)
    closeSQLsocket(); /* *** SQL Engine *** */

#ifdef WIN32
  termWinsock32();
#endif

  endNtop = 1;

#ifdef MEMORY_DEBUG
  traceEvent(TRACE_INFO, "===================================\n");
  termLeaks();
  traceEvent(TRACE_INFO, "===================================\n");
#endif

  sleep(3); /* Just to wait until threads complete */
  exit(0);
}
