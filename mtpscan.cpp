// mtpscan.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <windows.h>
#include <commctrl.h>
#include "..\inc\shmem.h"

//#define TEST
//#define AUTORESET

#define IOMSG	1
#define COMMSG	0
#define ERRMSG	1
#define ANLMSG	0
#define LIMDBG	0 // for analog change check

TCHAR	strCh[16];

HANDLE			m_hMap;
LPCHANNEL		pobj;
CHANNEL			obj;

#define BAUDRATE	CBR_9600
#define NUMTRM		(MAXTRM-1)

_trmdata	*pScanCont = NULL;

#define	trmin	pobj->TBUF
#define	trmout	pobj->TBUF

BOOL	bchg = FALSE;
#define BUFLEN	32
#define MODINT	4
int nmodint = 0;

int nTotal = -1, nTotalPre = -1;
int nOk = -1, nOkPre = -1;
int nErr = -1, nErrPre = -1;
int nNorsp = -1, nNorspPre = -1;
int nErrAcc = 0;

#ifndef MTMUX
#define IOCTL_MUX_SET		0x1001
#define IOCTL_MUX_RESET		0x1000
#define IOCTL_MUX_READ		0x1002
#define IOCTL_MUX_REBOOT	0x1003
#define IOCTL_MUX_CONFIG	0x1004

HANDLE	hComDev;

BOOL OpenCom(int chn)
{
	DWORD	i = 0;
	TCHAR	tbuf[32];
	DCB          commDCB;
	COMMTIMEOUTS commTimeouts;

	if (chn<0 || chn>=NUMCHN) return FALSE;
	_stprintf_s(tbuf, 32, _T("MX%02X:"), chn);
	{
	    if ((hComDev = CreateFile(tbuf, GENERIC_READ | GENERIC_WRITE, 0,
                              NULL, OPEN_EXISTING, 0, 0))
		    != INVALID_HANDLE_VALUE)
	    {
			// open ok
		} else {
			RETAILMSG(1, (TEXT("-mtscan : Unable to open %s, hComdev x%X\n"),tbuf, hComDev));
	        //return ERROR_OPEN_FAILED;
			return FALSE;
		}
	}
	return TRUE;
}

void CloseCom()
{
	if (hComDev != INVALID_HANDLE_VALUE) {
		CloseHandle(hComDev);
		hComDev = INVALID_HANDLE_VALUE;
	}
}

BOOL WriteCom(BYTE *pbuf, int len)
{
	DWORD rlen;
	BYTE buf[16];
	if (hComDev != INVALID_HANDLE_VALUE) {
		ReadFile(hComDev, buf, 16, &rlen, 0); // flush 
		WriteFile(hComDev, pbuf, len, &rlen, 0);
		return TRUE;
	}
	return FALSE;
	
}

DWORD ReadCom(BYTE *pbuf, int len)
{
	DWORD len1 = 0;
	if (ReadFile(hComDev, pbuf, len, &len1, 0)) return len1;
	else return 0;
}

void Reset()
{
	DeviceIoControl(hComDev, IOCTL_MUX_RESET, NULL, 0x0b, NULL, 0, NULL, NULL);
	Sleep(1000);
	DeviceIoControl(hComDev, IOCTL_MUX_RESET, NULL, 0x0a, NULL, 0, NULL, NULL);
	Sleep(5000);
}

DWORD SendPacket(UCHAR* buf, DWORD len)
{
	DWORD	len1;
	DeviceIoControl(hComDev, IOCTL_MUX_SET, buf, len, NULL, 0, &len1, NULL);
	return len1;
}

DWORD MuxRead()
{
	char buf[8];
	DWORD	len1, len=0;
	DeviceIoControl(hComDev, IOCTL_MUX_READ, buf, ++len, NULL, 0, &len1, NULL);
	return len1;
}

#else

#define DBGMSG	RETAILMSG
#define MSG_IO		1
#define MSG_COM		0
#define MSG_ERR		1
#define MSG_ANL		0
#define MSG_INFO	0

DWORD muxofs[16] = {
	0x0000, 0x0020, 0x0040, 0x0060,
	0x0100, 0x0120, 0x0140, 0x0160,
	0x0200, 0x0220, 0x0240, 0x0260,
	0x0300, 0x0320, 0x0340, 0x0360
};

#define uartread(ch, reg) (*(volatile unsigned char * const)(MBASE+muxofs[ch-1]+reg*4))
#define uartwrite(ch, reg, val) (*(volatile unsigned char * const)(MBASE+muxofs[ch-1]+reg*4)) = (val)

#define BASE_MUX	0xA4000000
PUCHAR	MBASE = (PUCHAR)BASE_MUX;

int hComDev = 0;

BOOL OpenCom(int chn)
{
	WORD	br;
	BYTE	reg;
	hComDev = chn;
	if (hComDev==0) return TRUE;
	uartwrite(hComDev, 7, 0x50+hComDev);
	reg = uartread(hComDev, 7);
	if (reg != 0x50+hComDev) {
		DBGMSG(MSG_ERR, (TEXT("Mux not detected on ch %B\n"), hComDev));
		return FALSE;
	}
	br = (WORD)(16000000.0/(9600*16.0));
	uartwrite(hComDev,3,0x80);
	uartwrite(hComDev,0,br & 0xff);
	uartwrite(hComDev,1,br >> 8);
	uartwrite(hComDev,3,0x03);
	uartwrite(hComDev,2,0x0f);
	uartwrite(hComDev,1,0x01);
	uartwrite(hComDev,4,0x0a); // 0x0b->0x0a 2015/1/6
	uartread(hComDev,6);
	uartread(hComDev,0);
	DBGMSG(MSG_INFO, (TEXT("Mux initialized on ch %B\n"), hComDev));
	return TRUE;
}

void CloseCom()
{
	DBGMSG(MSG_INFO,(TEXT("::: CloseCom()\r\n")));

	if (MBASE)
    {
        //VirtualFree(MBASE, 0x1000, MEM_RELEASE);
        MBASE = NULL;
    }
}

BOOL WriteCom(unsigned char *pbuf, int len)
{
	BYTE i;
	DBGMSG(MSG_COM, (TEXT("send(%d):"),len));
	for (i=0; i<len; i++) {
		uartwrite(hComDev, 0, pbuf[i]);
		DBGMSG(MSG_COM, (TEXT("%02x "), pbuf[i]));
	}
	return TRUE;
}

DWORD ReadCom(unsigned char  *pbuf, int len)
{
	BYTE	idx = 0;
	while ((uartread(hComDev, 5) & 1) && (idx<len)) {
		pbuf[idx++] = uartread(hComDev,0);
	}
	return idx;
}

UCHAR Read_LSR() 
{
	return uartread(hComDev, 5);
}

UCHAR Read_MCR() 
{
	return uartread(hComDev, 4);
}

void Write_LCR(UCHAR data) 
{
	uartwrite(hComDev, 3, data);
}

void Write_MCR(UCHAR data) 
{
	uartwrite(hComDev, 4, data);
}

void Write_DATA(UCHAR data) 
{
	uartwrite(hComDev, 0, data);
}

#define SERIAL_LSR_TEMT	(0x40)
#define SERIAL_8_DATA	(0x03)
#define SERIAL_MARK_PARITY	(0x28)
#define SERIAL_SPACE_PARITY	(0x38)

void sendaddr(UCHAR add)
{
	while ((Read_LSR() & SERIAL_LSR_TEMT)==0);
	Write_LCR(SERIAL_8_DATA | SERIAL_MARK_PARITY); // | SERIAL_2_STOP);
	Write_DATA(add);
	while ((Read_LSR() & SERIAL_LSR_TEMT)==0);
	Write_LCR(SERIAL_8_DATA | SERIAL_SPACE_PARITY);
}

void Reset()
{
	Write_MCR(0x0b);
	Sleep(1000);
	Write_MCR(0x0a);
	Sleep(5000);
}

DWORD SendPacket(UCHAR* pbuf, DWORD len)
{
	DWORD	len1;
	int  i;
	sendaddr(*pbuf++);
	for (i=1; i<len; i++) {
		while ((Read_LSR() & SERIAL_LSR_TEMT)==0);
		Write_DATA(*pbuf++);
	}		
	len1 = Read_MCR();
	return len1;
}

DWORD MuxRead()
{
	DWORD	len1;
	return Read_MCR();
}

#endif





#define SCAN_NONE	0x00
#define SCAN_NORSP	0x01
#define SCAN_CKERR	0x02
#define SCAN_CNTERR	0x03
#define SCAN_OK		0x10

UCHAR scanres[MAXTRM];

void scan(BYTE trm, BYTE cmd)
{
	DWORD	len1,len=0;
	UCHAR	buf[BUFLEN];
	WORD	data;
	bool	bRes = true;

	ReadCom(buf, BUFLEN); // read fifo clear
	if ((pScanCont->bin & LINE_SHORT)==0) {
		buf[len++] = trm;
		buf[len++] = cmd;
		buf[len] = (trm+cmd) & 0x7f;
		if (cmd==0x7f) buf[len] ^= 0x7f;
		//DeviceIoControl(hComDev, IOCTL_MUX_SET, buf, ++len, NULL, 0, &len1, NULL);
		len1 = SendPacket(buf, ++len);
		//RETAILMSG(1, (_T("len1=%x\r\n"), len1));		
		nTotal++;
#ifdef MTMUX
		Sleep(15);
#else
		Sleep(20);
#endif
	} else {
		//DeviceIoControl(hComDev, IOCTL_MUX_READ, buf, ++len, NULL, 0, &len1, NULL);
		len1 = MuxRead();
	}
	if (len1==0x0b) pScanCont->bin |= LINE_SHORT;
	else pScanCont->bin &= ~LINE_SHORT;	

	len = ReadCom(buf, BUFLEN);
	/*int tmo = 0;	
	while (len<4 && ++tmo<10) {
		Sleep(1);
		len += ReadCom(&buf[len], BUFLEN-len);
	}*/
	if (len>=4) {
		RETAILMSG(COMMSG, (_T("pscan(%s):len=%d %x %x %x %x\r\n"), strCh, len, buf[0], buf[1], buf[2], buf[3]));
		if (((buf[0]+buf[1]+buf[2]) & 0x7f)==buf[3]) {
			if (pobj->TBUF[trm][0].trm_type != TID_ANLS || pobj->TBUF[trm][0].trm_cerr) {
				pobj->TBUF[trm][0].trm_type = TID_ANLS;
				pobj->TBUF[trm][0].trm_cerr = 0;
				pobj->TBUF[trm][0].trm_updt = 1;
				RETAILMSG(COMMSG, (_T("back to live on %s id:%x, %d\r\n"), strCh, trm, data));
			}
			data = ((buf[2]<<7)+buf[1]) >> 2;
			data = data & 0x3ff; // 10 bit;
			
			// auto detect sensor type
			if ((pScanCont->bin & SCAN_SELF) && (data <= 1) && (pobj->TBUF[trm][0].in_type == 1)) {
				pobj->TBUF[trm][0].in_type = 0;
			} else 
			{
				if (pobj->TBUF[trm][0].in_val != data || ((trm % MODINT)==nmodint))
				{
					pobj->TBUF[trm][0].in_val = data;
					pobj->TBUF[trm][0].in_chg = 1;
				}		
			}
			pobj->TBUF[trm][TRMCNT].bsts = 0;
			if (pobj->TBUF[trm][TRMCNT].bin>0) pobj->TBUF[trm][TRMCNT].bin--;
			scanres[trm] = SCAN_OK;
		} else {
			RETAILMSG(ERRMSG, (_T("checksum err on %s id:%x\r\n"), strCh, trm));
			scanres[trm] = SCAN_CKERR;
			bRes = false;
		}
	} else if ((pScanCont->bin & LINE_SHORT)==0) {		
		bRes = false;
		if (len==0) {
			nNorsp++;
			scanres[trm] = SCAN_NORSP;
		} else {
			scanres[trm] = SCAN_CNTERR;
		}
	}
	if (!bRes) {
		nErr++;
		if (pobj->TBUF[trm][TRMCNT].bin<254) pobj->TBUF[trm][TRMCNT].bin++;		
		if (pobj->TBUF[trm][0].trm_cerr==0) {
			if (++pobj->TBUF[trm][TRMCNT].bsts>10) {
				pobj->TBUF[trm][0].trm_type = TID_CKID;
				pobj->TBUF[trm][0].trm_cerr = 1;
				pobj->TBUF[trm][0].trm_updt = 1;
				pobj->TBUF[trm][TRMCNT].bsts = 0;
			} else if (pobj->TBUF[trm][TRMCNT].bsts<6) {
				RETAILMSG(ERRMSG, (_T("timeout err on %s id:%x, len=%d\r\n"), strCh, trm, len));
			}
		}
	}
}

void scanning()
{
	int		trm;
	UCHAR	st, st1;
	DWORD dwstime = GetTickCount();
	UCHAR	buf[4], cmd=0x10;
	int		numouton = 0;
	
	if (nTotal>=0) {
		//if ((nErr-nErrPre)>10) {
		//	Sleep(1000); // for com error/recover repeating
		//}
		nTotalPre = nTotal;
		nOkPre = nOk;
		nErrPre = nErr;
		nNorspPre = nNorsp;
	}
	nTotal = 0;
	nOk = 0;
	nErr = 0;
	nNorsp = 0;
	numouton = 0;
	for (trm=1; trm<NUMTRM; trm++) {
		if (st!=TID_NONE) {
			if (pobj->TBUF[trm][0].out_cur>0) numouton++;
		}
	}
	for (trm=1; trm<NUMTRM; trm++) 
	//trm = 1;
	{
		scanres[trm] = SCAN_NONE;
		st = pobj->TBUF[trm][0].trm_type;
		st1 = obj.TBUF[trm][0].trm_type;
		if ((st!=TID_NONE) && (st1==TID_NONE)) {
			// enable
			obj.TBUF[trm][0].trm_type = TID_CKID;			
		} else if ((st1!=TID_NONE) && (st==TID_NONE)) {
			// disable
			obj.TBUF[trm][0].dwdata = TRMDATA_INIT;
		}
		if (st!=TID_NONE) {
			cmd = 0x10;
			if (pobj->TBUF[trm][0].in_stat>0) {
				if (pobj->TBUF[trm][0].out_cur==0) {
					//if (numouton<20) 
					{
						pobj->TBUF[trm][0].out_cur = 1;
						numouton++;
					}
				}							
				if (pobj->TBUF[trm][0].out_cur>0) {
					cmd |= 0x1;
				}
			} else {
				if (pobj->TBUF[trm][0].out_cur>0) {
					if (numouton>0) numouton--;
					pobj->TBUF[trm][0].out_cur = 0;
				}						
			}			
			if (pobj->TBUF[trm][0].in_type>0) cmd |= 0x2;
			scan(trm, cmd);
			Sleep(1);
		}
		if ((pScanCont->bin & SCAN_LIVE)==0) break;
	}
	if (++nmodint>=MODINT) nmodint = 0;

	/*numouton = 0;
	for (trm=1; trm<NUMTRM; trm++) {
		if ((scanres[trm] & 0x0f)>SCAN_NORSP) {
			cmd = 0x10;
			if (pobj->TBUF[trm][0].in_stat>0) {
				if (++numouton<20) {
					cmd |= 0x1;
				}
			}
			if (pobj->TBUF[trm][0].in_type>0) cmd |= 0x2;
			scan(trm, cmd);
			Sleep(1);
		} else if ((scanres[trm] & 0x0f)==SCAN_NORSP) {
			++pobj->TBUF[trm][TRMCNT].bsts;
		}
	}*/

#ifdef AUTORESET
	if (nTotal>128) {
		if (nTotalPre>=0) {
			if (nErr>nErrPre) {
				nErrAcc += nErr-nErrPre;
				if (nErrAcc>10) {
					Reset();
					pScanCont->bout++;
					pScanCont->bout &= 0x0f;
					nErrAcc = 0;
				}
			} else if (nErr<nErrPre) {
				nErrAcc = 0;
			} else if (nErrAcc>0) {
				nErrAcc--;
			}
		}
	}
#endif
	RETAILMSG(LIMDBG, (TEXT("scantime = %d ms\n"), GetTickCount()-dwstime));
}

#ifdef _WIN32_WCE
int _tmain(int argc, _TCHAR* argv[])
#else
int main(int argc, char* argv[])	
#endif
{
	LPTSTR    lpCmdLine = argv[1];
	int chn,trm;
	DWORD	tic;

	if (lpCmdLine==NULL) {
		RETAILMSG(1,(_T("No parameter !\n")));
		return -1;
	}
	chn = _ttoi(lpCmdLine);

	//_stprintf_s(strCh, 16, _T("CH%x"), chn);
	if (argc > 2) {
		_tcscpy_s(strCh, 16, argv[2]);
	}
	else {
#ifndef MTMUX
		_stprintf_s(strCh, 16, _T("CH%x"), chn);
#else
		_stprintf(strCh, _T("CH%x"), chn);
#endif
	}
	
	if (!OpenCom(chn)) {
		RETAILMSG(1,(_T("Open comm(%d) fail !\n"), chn));
		return -1;
	} 
	m_hMap = CreateFileMapping(INVALID_HANDLE_VALUE, NULL,
							PAGE_READWRITE | SEC_COMMIT,
							0,
							sizeof(CHANNEL),
							strCh);
	if (m_hMap==NULL) {
		RETAILMSG(1,(_T("%s:CreateFileMapping Error\n"), strCh));
		return -1;
	}
	pobj = (LPCHANNEL)MapViewOfFile(m_hMap, 
					FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CHANNEL));
	if (pobj==NULL) {
		if (m_hMap) CloseHandle(m_hMap);
		RETAILMSG(1,(_T("%s:MapViewOfFile Error\n"),strCh));
		return -1;
	}	
	RETAILMSG(1, (_T("%s:mtpscan start (pobj=%X)\n"), strCh, pobj));
	
	for (trm=1; trm<NUMTRM; trm++) {
		pobj->TBUF[trm][TRMCNT].bin = 0;
		pobj->TBUF[trm][TRMCNT].bsts = 0;
#ifdef TEST
		if (trm<0x20) pobj->TBUF[trm][0].trm_type = TID_CKID; 
		else pobj->TBUF[trm][0].trm_type = TID_NONE;
#endif
		obj.TBUF[trm][0].dwdata = TRMDATA_INIT;		
	}

	pScanCont = &pobj->TBUF[0][IDX_STATE];
#ifdef TEST
	pScanCont->bin |= SCAN_READY | SCAN_LIVE | SCAN_EN;
#else
	pScanCont->bin |= SCAN_READY;// | SCAN_LIVE | SCAN_EN;
#endif

#ifdef _WIN32_WCE
	CeSetThreadPriority(GetCurrentThread(), CE_THREAD_PRIO_256_HIGHEST); //100);
#endif
#define TMOUT	100 // 10 sec
	pobj->TBUF[0][IDX_TIMECNT].dwdata = TMOUT;
	pScanCont->bin |= SCAN_READY | SCAN_LIVE;
	Reset();
	pScanCont->bout = 0;
	while ((pScanCont->bin & SCAN_LIVE) && (pobj->TBUF[0][IDX_TIMECNT].dwdata>0)) {
		if (pScanCont->bin & SCAN_EN) {
			tic = GetTickCount();
			scanning();
			pobj->TBUF[0][IDX_SCANTIME].dwdata = GetTickCount()-tic;
#ifndef TEST
			pScanCont->bin &= ~SCAN_EN;
#endif
			pobj->TBUF[0][IDX_TIMECNT].dwdata = TMOUT;
			pScanCont->bout++;
		}
		if (pScanCont->bin & SCAN_BROADCAST) {
			pScanCont->bin &= ~SCAN_BROADCAST;
			if (pobj->TBUF[0][IDX_STATE].bsts == RESETCMD) Reset();
		}
		//if (pobj->TBUF[0][IDX_TIMECNT].dwdata>0) --pobj->TBUF[0][IDX_TIMECNT].dwdata;
		Sleep(100);
	}
	pScanCont->bin &= ~SCAN_READY;
	RETAILMSG(1, (_T("%s:mtpscan exit \n"), strCh, pobj));
	UnmapViewOfFile(pobj);
	if (m_hMap) CloseHandle(m_hMap);

	return 0;
}

