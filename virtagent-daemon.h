/*
 * virt-agent - host/guest RPC daemon functions
 *
 * Copyright IBM Corp. 2010
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#define GUEST_AGENT_SERVICE_ID "virtagent"
#define GUEST_AGENT_PATH "/tmp/virtagent-guest.sock"
#define HOST_AGENT_SERVICE_ID "virtagent-host"
#define HOST_AGENT_PATH "/tmp/virtagent-host.sock"
#define VA_GETFILE_MAX 1 << 30
#define VA_FILEBUF_LEN 16384
#define VA_DMESG_LEN 16384

int va_server_loop(int listen_fd, bool is_host);
