#include <stdio.h>
#include <linux/if_bridge.h>
#include <linux/rtnetlink.h>
#include <linux/netlink.h>
#include <unistd.h>
#include <net/if.h>
#include "libnetlink.h"
#include "ll_map.h"

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

	req.ifm.ifi_index = ll_name_to_index(ifName);
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
