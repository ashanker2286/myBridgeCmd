#include <stdio.h>
#include <string.h>
#include <linux/if_bridge.h>
#include <linux/rtnetlink.h>
#include <linux/netlink.h>
#include <linux/if_link.h>
#include <sys/socket.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
//#include <stdlib.h>
#include <net/if.h>

#define NLMSG_TAIL(nmsg) \
        ((struct rtattr *) (((void *) (nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

int rcvbuf = 1024 * 1024;

struct rtnl_handle
{
        int                     fd;
        struct sockaddr_nl      local;
        struct sockaddr_nl      peer;
        __u32                   seq;
        __u32                   dump;
};


static unsigned get_ifIdx_from_ifName(char *ifName) {
        unsigned idx;

        if (ifName == NULL)
                return 0;

        idx = if_nametoindex(ifName);
        if (idx == 0) 
                sscanf(ifName, "if%u", &idx);
        return idx;
}


static int addattr_l(struct nlmsghdr *n, int maxlen, int type, const void *data, int alen) {
	int len = RTA_LENGTH(alen);
	struct rtattr *rta;

        if (NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len) > maxlen) {
                fprintf(stderr, "addattr_l ERROR: message exceeded bound of %d\n",maxlen);
                return -1;
        }
        rta = NLMSG_TAIL(n);
        rta->rta_type = type;
        rta->rta_len = len;
        memcpy(RTA_DATA(rta), data, alen);
        n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);
        return 0;
}


static int addattr16(struct nlmsghdr *n, int maxlen, int type, __u16 data)
{
        return addattr_l(n, maxlen, type, &data, sizeof(__u16));
}



static struct rtattr *addattr_nest(struct nlmsghdr *n, int maxlen, int type) {
	struct rtattr *nest = NLMSG_TAIL(n);
	int retVal = 0;

	retVal = addattr_l(n, maxlen, type, NULL, 0);
	if (retVal < 0) {
		return NULL;
	}
	return nest;
}


static int addattr_nest_end(struct nlmsghdr *n, struct rtattr *nest)
{
        nest->rta_len = (void *)NLMSG_TAIL(n) - (void *)nest;
        return n->nlmsg_len;
}


static int rtnl_open_byproto(struct rtnl_handle *rth, unsigned subscriptions,
                      int protocol)
{
        socklen_t addr_len;
        int sndbuf = 32768;

        memset(rth, 0, sizeof(*rth));

        rth->fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, protocol);
        if (rth->fd < 0) {
                perror("Cannot open netlink socket");
                return -1;
        }

        if (setsockopt(rth->fd,SOL_SOCKET,SO_SNDBUF,&sndbuf,sizeof(sndbuf)) < 0) {
                perror("SO_SNDBUF");
                return -1;
        }

        if (setsockopt(rth->fd,SOL_SOCKET,SO_RCVBUF,&rcvbuf,sizeof(rcvbuf)) < 0) {
                perror("SO_RCVBUF");
                return -1;
        }

        memset(&rth->local, 0, sizeof(rth->local));
        rth->local.nl_family = AF_NETLINK;
        rth->local.nl_groups = subscriptions;

        if (bind(rth->fd, (struct sockaddr*)&rth->local, sizeof(rth->local)) < 0) {
                perror("Cannot bind netlink socket");
                return -1;
        }
        addr_len = sizeof(rth->local);
        if (getsockname(rth->fd, (struct sockaddr*)&rth->local, &addr_len) < 0) {
                perror("Cannot getsockname");
                return -1;
        }
        if (addr_len != sizeof(rth->local)) {
                fprintf(stderr, "Wrong address length %d\n", addr_len);
                return -1;
        }
        if (rth->local.nl_family != AF_NETLINK) {
                fprintf(stderr, "Wrong address family %d\n", rth->local.nl_family);
                return -1;
        }
        rth->seq = time(NULL);
        return 0;
}

static int rtnl_open(struct rtnl_handle *rth, unsigned subscriptions)
{
        return rtnl_open_byproto(rth, subscriptions, NETLINK_ROUTE);
}

static int rtnl_talk(struct rtnl_handle *rtnl, struct nlmsghdr *n, pid_t peer,
              unsigned groups, struct nlmsghdr *answer)
{
        int status;
        unsigned seq;
        struct nlmsghdr *h;
        struct sockaddr_nl nladdr;
        struct iovec iov = {
                .iov_base = (void*) n,
                .iov_len = n->nlmsg_len
        };
        struct msghdr msg = {
                .msg_name = &nladdr,
                .msg_namelen = sizeof(nladdr),
                .msg_iov = &iov,
                .msg_iovlen = 1,
        };
        char   buf[16384];

        memset(&nladdr, 0, sizeof(nladdr));
        nladdr.nl_family = AF_NETLINK;
        nladdr.nl_pid = peer;
        nladdr.nl_groups = groups;

        n->nlmsg_seq = seq = ++rtnl->seq;

        if (answer == NULL)
                n->nlmsg_flags |= NLM_F_ACK;

        status = sendmsg(rtnl->fd, &msg, 0);

        if (status < 0) {
                perror("Cannot talk to rtnetlink");
                return -1;
        }

        memset(buf,0,sizeof(buf));

        iov.iov_base = buf;

        while (1) {
                iov.iov_len = sizeof(buf);
                status = recvmsg(rtnl->fd, &msg, 0);

                if (status < 0) { 
                        if (errno == EINTR || errno == EAGAIN)
                                continue;
                        fprintf(stderr, "netlink receive error %s (%d)\n",
                                strerror(errno), errno);
                        return -1;
                }
                if (status == 0) {
                        fprintf(stderr, "EOF on netlink\n");
                        return -1;
                }
                if (msg.msg_namelen != sizeof(nladdr)) {
                        fprintf(stderr, "sender address length == %d\n", msg.msg_namelen);
			return -1;
                }
                for (h = (struct nlmsghdr*)buf; status >= sizeof(*h); ) {
                        int len = h->nlmsg_len;
                        int l = len - sizeof(*h);

                        if (l < 0 || len>status) {
                                if (msg.msg_flags & MSG_TRUNC) {
                                        fprintf(stderr, "Truncated message\n");
                                        return -1;
                                }
                                fprintf(stderr, "!!!malformed message: len=%d\n", len);
				return -1;
                        }

                        if (nladdr.nl_pid != peer ||
                            h->nlmsg_pid != rtnl->local.nl_pid ||
                            h->nlmsg_seq != seq) {
                                /* Don't forget to skip that message. */
                                status -= NLMSG_ALIGN(len);
                                h = (struct nlmsghdr*)((char*)h + NLMSG_ALIGN(len));
                                continue;
                        }

                        if (h->nlmsg_type == NLMSG_ERROR) {
                                struct nlmsgerr *err = (struct nlmsgerr*)NLMSG_DATA(h);
                                if (l < sizeof(struct nlmsgerr)) {
                                        fprintf(stderr, "ERROR truncated\n");
                                } else {
                                        if (!err->error) {
                                                if (answer)
                                                        memcpy(answer, h, h->nlmsg_len);
                                                return 0;
                                        }

                                        fprintf(stderr, "RTNETLINK answers: %s\n", strerror(-err->error));
                                        errno = -err->error;
                                }
                                return -1;
                        }
                        if (answer) {
                                memcpy(answer, h, h->nlmsg_len);
                                return 0;
                        }

                        fprintf(stderr, "Unexpected reply!!!\n");

                        status -= NLMSG_ALIGN(len);
                        h = (struct nlmsghdr*)((char*)h + NLMSG_ALIGN(len));
                }
                if (msg.msg_flags & MSG_TRUNC) {
                        fprintf(stderr, "Message truncated\n");
                        continue;
                }
                if (status) {
                        fprintf(stderr, "!!!Remnant of size %d\n", status);
                        return -1;
                }
        }
}

static void rtnl_close(struct rtnl_handle *rth)
{
        if (rth->fd >= 0) {
                close(rth->fd);
                rth->fd = -1;
        }
}



int setInterfacePvid(char *ifName, int vlanId, int bridgeFlag) {
        struct {
                struct nlmsghdr         n;
                struct ifinfomsg        ifm;
                char                    buf[1024];
        } req;
	struct bridge_vlan_info vinfo;
	struct rtattr *afspec;
	struct rtnl_handle rth = { .fd = -1 };
	int retVal = 0;

	if (ifName == NULL) {
		fprintf(stderr, "Invalid interface name\n");
		return -1;
	}

	if ((vlanId < 0) && (vlanId >= 4096)) {
		fprintf(stderr, "Invalid VlanId\n");
		return -1;
	}

	memset(&vinfo, 0, sizeof(vinfo));
        memset(&req, 0, sizeof(req));

        req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
        req.n.nlmsg_flags = NLM_F_REQUEST;
        req.n.nlmsg_type = RTM_SETLINK;
        req.ifm.ifi_family = PF_BRIDGE;

	vinfo.flags |= BRIDGE_VLAN_INFO_PVID | BRIDGE_VLAN_INFO_UNTAGGED;

	req.ifm.ifi_index = get_ifIdx_from_ifName(ifName);
	if (req.ifm.ifi_index == 0) {
		fprintf(stderr, "Cannot find bridge device \"%s\"\n", ifName);
		return -1;
	} else {
		printf("IfIndex of IfName(%s) is %u\n", ifName, req.ifm.ifi_index);
	}

	vinfo.vid = vlanId;
	afspec = addattr_nest(&req.n, sizeof(req), IFLA_AF_SPEC);

	if (afspec == NULL) {
		fprintf(stderr, "RTLINK Route Attribute is NULL\n");
		return -1;
	}

        if (bridgeFlag)
                addattr16(&req.n, sizeof(req), IFLA_BRIDGE_FLAGS, BRIDGE_FLAGS_SELF);

	retVal = addattr_l(&req.n, sizeof(req), IFLA_BRIDGE_VLAN_INFO, &vinfo,
                  	sizeof(vinfo));
	if (retVal < 0) {
		fprintf(stderr, "Unable to create RTLINK Attribute msg\n");
		return -1;
	}


        addattr_nest_end(&req.n, afspec);
	if (rtnl_open(&rth, 0) < 0) {
		fprintf(stderr, "Unable to open RTLINK\n");
		return -1;
	}

	if (rtnl_talk(&rth, &req.n, 0, 0, NULL) < 0) {
		fprintf(stderr, "Unable to sent RTLINK Messgae\n");
		return -1;
	}

	rtnl_close(&rth);
	return 0;
}
