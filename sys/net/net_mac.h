/*
 * Direct replacements for the 2.11BSD PDP networking overlay macros on
 * flat-address-space machine ports.
 */
#ifndef _NET_NET_MAC_H_
#define _NET_NET_MAC_H_

struct mbuf;
struct socket;

int netcopyout();
struct socket *asoqremque();
int connwhile();
int soacc1();
int soaccept();
int sobind();
int soclose();
int soconnect();
int soconnect2();
int socreate();
int sogetnam();
int sogetopt();
int sogetpeer();
int solisten();
int soo_ioctl();
int soo_select();
int soo_stat();
int soreceive();
int sosend();
int sosetopt();
int soshutdown();

#define SLEEP(chan, pri)                sleep((caddr_t)(chan), (pri))
#define WAKEUP(chan)                    wakeup((caddr_t)(chan))
#define SELWAKEUP(p, coll)              selwakeup((p), (coll))
#define TIMEOUT(fun, arg, t)            timeout((fun), (arg), (t))
#define GSIGNAL(pgrp, sig)              gsignal((pgrp), (sig))
#define NETPFIND(pid)                   pfind((pid))
#define NETPSIGNAL(p, sig)              psignal((p), (sig))

#define SOCREATE(dom, aso, type, proto) socreate((dom), (aso), (type), (proto))
#define SOACC1(so)                      soacc1((so))
#define ASOQREMQUE(so, n)               asoqremque((so), (n))
#define SOBIND(so, nam)                 sobind((so), (nam))
#define CONNWHILE(so)                   connwhile((so))
#define SOLISTEN(so, backlog)           solisten((so), (backlog))
#define SOACCEPT(so, nam)               soaccept((so), (nam))
#define SOCLOSE(so)                     soclose((so))
#define SOCON1(so, nam)                 soconnect((so), (nam))
#define SOCON2(so1, so2)                soconnect2((so1), (so2))
#define SOGETNAM(so, m)                 sogetnam((so), (m))
#define SOGETPEER(so, m)                sogetpeer((so), (m))
#define SOGETOPT(so, level, opt, mp)    sogetopt((so), (level), (opt), (mp))
#define SOSETOPT(so, level, opt, m0)    sosetopt((so), (level), (opt), (m0))
#define SOSEND(so, nam, uio, fl, r)     sosend((so), (nam), (uio), (fl), (r))
#define SORECEIVE(so, nam, uio, fl, r)  soreceive((so), (nam), (uio), (fl), (r))
#define SOSHUTDOWN(so, how)             soshutdown((so), (how))

#define M_FREE(m)                       (void)m_free((m))
#define M_FREEM(m)                      m_freem((m))
#define NETCOPYOUT(m, dst, lenp)        netcopyout((m), (dst), (lenp))

#define SOO_IOCTL(fp, cmd, data)        soo_ioctl((fp)->f_socket, (cmd), (data))
#define SOO_SELECT(fp, which)           soo_select((fp)->f_socket, (which))
#define SOO_STAT(so, ub)                soo_stat((so), (ub))

#endif
