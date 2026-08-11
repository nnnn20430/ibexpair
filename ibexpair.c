// SPDX-License-Identifier: GPL-3.0

#include <linux/types.h>
#include <linux/input.h>
#include <linux/hidraw.h>
#include <libudev.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/param.h>

const int REPORT_ID_CONTROLLER = 0x01;
const int REPORT_ID_PUCK = 0x02;

char uuid[8] = {0};
char puck_serial[16] = {0};
char ctrl_serial[16] = {0};

static int hex(const char *s) { return (int)strtol(s, NULL, 16); }

int find_device(int pvid, int ppid, int pifn, char *out, int len) {
	struct udev *u = udev_new();
	struct udev_enumerate *e = udev_enumerate_new(u);
	const char *devnode;
	int devlen;

	udev_enumerate_add_match_subsystem(e, "hidraw");
	udev_enumerate_scan_devices(e);

	struct udev_list_entry *l, *devices = udev_enumerate_get_list_entry(e);
	udev_list_entry_foreach(l, devices) {
		struct udev_device *h = udev_device_new_from_syspath(u, udev_list_entry_get_name(l));
		struct udev_device *usb = udev_device_get_parent_with_subsystem_devtype(h, "usb", "usb_device");
		struct udev_device *iface = udev_device_get_parent_with_subsystem_devtype(h, "usb", "usb_interface");
		if (!usb) { udev_device_unref(h); continue; }

		const char *vid = udev_device_get_sysattr_value(usb, "idVendor");
		const char *pid = udev_device_get_sysattr_value(usb, "idProduct");
		const char *ifn = udev_device_get_sysattr_value(iface, "bInterfaceNumber");

		if (
			vid && pid && ifn &&
			hex(vid) == pvid &&
			hex(pid) == ppid &&
			hex(ifn) == pifn
		) {
			devnode = udev_device_get_devnode(h);
			devlen = strlen(devnode);
			memcpy(out, devnode, MIN(devlen, len));
			udev_device_unref(h);
			break;
		}
		udev_device_unref(h);
	}

	udev_enumerate_unref(e);
	udev_unref(u);

	if (devnode == NULL) return -1;
	return 0;
}

void generate_uuid() {
	char c = 0;
	int i = 0, fd = open("/dev/urandom", O_RDONLY);
	while (i < sizeof(uuid)) {
		read(fd, &c, 1);
		if (c) uuid[i++] = c;
	}
}

int get_controller_serial(int dev) {
	int i = 0, ret = 0;
	char buf[64] = {0};

	// Set Feature
	buf[i] = REPORT_ID_CONTROLLER; i++;
	buf[i] = 0xae; i++;
	buf[i] = 0x15; i++;
	buf[i] = 0x01; i++;
	ret = ioctl(dev, HIDIOCSFEATURE(64), buf);
	if (ret < 0) {
		perror("Controller not answering on pogo pins");
		return -1;
	}

	printf("Getting controller info over pogo pins");
	fflush(stdout);

	// clear buf
	memset(buf, 0, sizeof(buf));

	// Get Feature
	buf[0] = REPORT_ID_CONTROLLER;
	for (i = 0; i < 10; i++) {
		ret = ioctl(dev, HIDIOCGFEATURE(64), buf);
		if (ret < 0) {
			printf("."); fflush(stdout);
			usleep(100*1000);
			continue;
		}
		printf("\n");
		printf("Controller serial number: ");
		for (i = 4; i <= MIN(16, ret); i++) {
			printf("%c", buf[i]);
			ctrl_serial[i-4] = buf[i];
		}
		printf("\n");
		return 0;
	}

	return -1;
}

int get_puck_serial(int dev) {
	int i = 0, ret = 0;
	char buf[64] = {0};

	// Set Feature
	buf[i] = REPORT_ID_PUCK; i++;
	buf[i] = 0xae; i++;
	buf[i] = 0x15; i++;
	buf[i] = 0x01; i++;
	ret = ioctl(dev, HIDIOCSFEATURE(64), buf);
	if (ret < 0) {
		perror("Puck not responding to commands");
		return -1;
	}

	printf("Getting puck info");
	fflush(stdout);

	// clear buf
	memset(buf, 0, sizeof(buf));

	// Get Feature
	buf[0] = REPORT_ID_PUCK;
	for (i = 0; i < 10; i++) {
		ret = ioctl(dev, HIDIOCGFEATURE(64), buf);
		if (ret < 0) {
			printf("."); fflush(stdout);
			usleep(100*1000);
			continue;
		}
		printf("\n");
		printf("Puck serial number: ");
		for (i = 4; i <= MIN(16, ret); i++) {
			printf("%c", buf[i]);
			puck_serial[i-4] = buf[i];
		}
		printf("\n");
		return 0;
	}

	return -1;
}

int write_bonding_puck(int dev) {
	int i = 0, ret = 0;
	char buf[64] = {0};

	// Set Feature
	buf[i] = REPORT_ID_PUCK; i++;
	buf[i] = 0xa2; i++;
	buf[i] = sizeof(uuid) + sizeof(ctrl_serial); i++;
	memcpy(&buf[i], uuid, MIN(sizeof(uuid), sizeof(buf)-i)); i+=sizeof(uuid);
	memcpy(&buf[i], ctrl_serial, MIN(sizeof(ctrl_serial), sizeof(buf)-i));
	ret = ioctl(dev, HIDIOCSFEATURE(64), buf);
	if (ret < 0) {
		perror("Failed writing bonding info to puck slot");
		return -1;
	}
	printf("Wrote bonding info to puck slot\n");
	return 0;
}

int write_bonding_controller(int dev, int slot) {
	int i = 0, ret = 0;
	char buf[64] = {0};
	const char *key_bond = (slot == 1 ? "esb/bond" : "esb/bond_2");

	// Set Feature
	buf[i] = REPORT_ID_CONTROLLER; i++;
	buf[i] = 0xee; i++;
	buf[i] = (strlen(key_bond)+1) + sizeof(uuid) + sizeof(puck_serial); i++;
	memcpy(&buf[i], key_bond, MIN((strlen(key_bond)+1), sizeof(buf)-i)); i+=(strlen(key_bond)+1);
	memcpy(&buf[i], uuid, MIN(sizeof(uuid), sizeof(buf)-i)); i+=sizeof(uuid);
	memcpy(&buf[i], puck_serial, MIN(sizeof(puck_serial), sizeof(buf)-i));
	ret = ioctl(dev, HIDIOCSFEATURE(64), buf);
	if (ret < 0) {
		perror("Failed writing bonding info to controller slot");
		return -1;
	}

	printf("Wrote bonding info to controller slot\n");

	// clear buf
	i = 0; memset(buf, 0, sizeof(buf));

	// Set Feature
	buf[i] = REPORT_ID_CONTROLLER; i++;
	buf[i] = 0xef; i++;
	buf[i] = (strlen(key_bond)+1); i++;
	memcpy(&buf[i], key_bond, MIN((strlen(key_bond)+1), sizeof(buf)-i));
	ret = ioctl(dev, HIDIOCSFEATURE(64), buf);
	if (ret < 0) {
		perror("Failed to commit bonding info to controller slot");
		return -1;
	}

	printf("Commited controller bonding info\n");

	return 0;
}

int reboot_controller(int dev) {
	int i = 0, ret = 0;
	char buf[64] = {0};

	// Set Feature
	buf[i] = REPORT_ID_CONTROLLER; i++;
	buf[i] = 0x95; i++;
	buf[i] = 0x04; i++;
	buf[i] = 0x52; i++;
	buf[i] = 0xaf; i++;
	buf[i] = 0x27; i++;
	buf[i] = 0xa4; i++;
	ret = ioctl(dev, HIDIOCSFEATURE(64), buf);
	if (ret < 0) {
		perror("Failed to reboot controller in wireless mode");
		return -1;
	}

	printf("Controller rebooted\n");

	return 0;
}

int main(int argc, char **argv) {
	int opt = 0;
	int puck_slot = 1, ctrl_slot = 1;
	int fd_slot = -1, fd_pogo = -1;
	char devnode_slot[256] = {0};
	char devnode_pogo[256] = {0};

	while ((opt = getopt(argc, argv, "p:c:")) != -1) {
		switch (opt) {
		case 'p':
			puck_slot = atoi(optarg);
			if (puck_slot < 1 || puck_slot > 4) {
				fprintf(stderr, "Puck slot %i outside range (1-4)\n", puck_slot);
				return 1;
			}
			break;
		case 'c':
			ctrl_slot = atoi(optarg);
			if (ctrl_slot < 1 || ctrl_slot > 2) {
				fprintf(stderr, "Controller slot %i outside range (1-2)\n", ctrl_slot);
				return 1;
			}
			break;
		default:
			fprintf(stderr, "Usage: %s [-p 1-4] [-c 1-2]\n", argv[0]);
		}
	}

	find_device(0x28de, 0x1304, puck_slot+1, devnode_slot, sizeof(devnode_slot));
	find_device(0x28de, 0x1304, 6, devnode_pogo, sizeof(devnode_pogo));

	fd_slot = open(devnode_slot, O_RDWR|O_NONBLOCK);
	fd_pogo = open(devnode_pogo, O_RDWR|O_NONBLOCK);

	if (fd_slot < 0 || fd_pogo < 0) {
		perror("Unable to open devices");
		return 1;
	}

	generate_uuid();
	if(get_controller_serial(fd_pogo) < 0) return 1;
	if(get_puck_serial(fd_slot) < 0) return 1;
	if(write_bonding_puck(fd_slot) < 0) return 1;
	if(write_bonding_controller(fd_pogo, ctrl_slot) < 0) return 1;
	if(reboot_controller(fd_pogo) < 0) return 1;

	close(fd_slot);
	close(fd_pogo);

	return 0;
}
