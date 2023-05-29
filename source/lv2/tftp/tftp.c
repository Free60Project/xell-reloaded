#include <stdlib.h>
#include <xetypes.h>

#include <lwip/mem.h>
#include <lwip/memp.h>
#include <lwip/sys.h>

#include <lwip/stats.h>

#include <lwip/dhcp.h>
#include <lwip/inet.h>
#include <lwip/ip.h>
#include <lwip/tcp.h>
#include <lwip/udp.h>
#include <netif/etharp.h>

#include "config.h"
#include "file.h"
#include <elf/elf.h>
#include <network/network.h>
#include <ppc/timebase.h>

#define TFTP_MAX_RETRIES 2

#define TFTP_STATE_RRQ_SEND 0
#define TFTP_STATE_DATA_RECV 1
#define TFTP_STATE_ERROR 2
#define TFTP_STATE_FINISH 3

#define TFTP_OPCODE_RRQ 1
#define TFTP_OPCODE_WRQ 2
#define TFTP_OPCODE_DATA 3
#define TFTP_OPCODE_ACK 4
#define TFTP_OPCODE_ERROR 5
#define TFTP_OPCODE_OACK 6

typedef struct {
  int state;
  int result;
  const char *error_msg;
  struct udp_pcb *pcb;

  ip_addr_t server_addr;
  uint16_t server_port;

  void *data;
  size_t data_offset;
  size_t data_maxlen;

  int tries;
  uint64_t start_time;
  uint64_t last_recv;

  uint32_t current_block;
  uint32_t block_size;
} tftp_state_t;

extern char *kboot_tftp;
extern void console_clrline();

int send_ack(struct udp_pcb *pcb, ip_addr_t server_addr, uint16_t port, uint32_t block) {
  int rc = 0;
  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 4, PBUF_RAM);

  if (!p) {
    printf("internal error: out of memory!\n");
    return -1;
  }

  unsigned char *d = p->payload;

  *(uint16_t *)(d + 0) = TFTP_OPCODE_ACK;
  *(uint16_t *)(d + 2) = block;

  if (udp_sendto(pcb, p, &server_addr, port)) {
    printf("TFTP: Failed to send ACK packet.\n");
    rc = -1;
  }

  pbuf_free(p);
  return rc;
}

static int send_rrq(struct udp_pcb *pcb, ip_addr_t server_addr, uint16_t port,
                    const char *file, const char *mode, uint16_t block_size) {
  int rc = 0;
  if (block_size > 64000) {
    printf("send_rrq: block_size too big!\n");
    return -1;
  }

  uint16_t buffer_len = 2 + strlen(file) + 1 + strlen(mode) + 1;
  if (block_size != 512) {
    buffer_len += strlen("blksize") + 1;
    buffer_len += snprintf(NULL, 0, "%i", block_size) + 1;
  }

  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, buffer_len, PBUF_RAM);
  if (!p) {
    printf("internal error: out of memory! (couldn't allocate %d bytes)\n",
           (int)buffer_len);
    return -1;
  }

  // Construct the payload.
  uint8_t *d = p->payload;

  *(uint16_t *)d = TFTP_OPCODE_RRQ;
  d += 2;

  strcpy((char*)d, file);
  d += strlen(file) + 1;

  strcpy((char*)d, mode);
  d += strlen(mode) + 1;

  if (block_size != 512) {
    strcpy((char*)d, "blksize");
    d += strlen("blksize") + 1;

    d += sprintf((char*)d, "%i", block_size);
  }

  if (udp_sendto(pcb, p, &server_addr, port) != 0) {
    console_clrline();
    printf("TFTP: Failed to send RRQ packet.\n");
    rc = -1;
  }

  pbuf_free(p);
  return rc;
}

static int send_error(struct udp_pcb *pcb, ip_addr_t server_addr,
                      uint16_t port, uint16_t code, const char *message) {
  int rc = 0;
  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 4 + strlen(message) + 1, PBUF_RAM);

  if (!p) {
    printf("internal error: out of memory!\n");
    return -1;
  }

  unsigned char *d = p->payload;
  *(uint16_t *)(d + 0) = TFTP_OPCODE_ERROR;
  *(uint16_t *)(d + 2) = code;
  strcpy((char *)d + 4, message);

  if (udp_sendto(pcb, p, &server_addr, port)) {
    rc = -1;
  }

  pbuf_free(p);
  return rc;
}

static void tftp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                      struct ip_addr *addr, u16_t port) {
  tftp_state_t *tftp_state = arg;
  unsigned char *d = p->payload;
  if (!addr || addr->addr != tftp_state->server_addr.addr) {
    // Not who we're listening for.
    return;
  }

  if (tftp_state->server_port == 0) {
    // "bind" this state to the port.
    tftp_state->server_port = port;
  } else if (tftp_state->server_port != port) {
    // Not us.
    return;
  }

  if (p->tot_len < 2) {
    // Malformed packet.
    tftp_state->state = TFTP_STATE_ERROR;
    tftp_state->error_msg = "Malformed packet (<2 bytes).";
    tftp_state->result = -1;
  }

  uint16_t opcode = *(uint16_t *)d;

  // Opcode
  switch (opcode) {
  case TFTP_OPCODE_RRQ:
  case TFTP_OPCODE_WRQ: {
    // ??? this is invalid
    tftp_state->state = TFTP_STATE_ERROR;
    tftp_state->error_msg = "Invalid WRQ/RRQ opcode received.";
    tftp_state->result = -1;
  } break;

  case TFTP_OPCODE_DATA: {
    uint16_t block = *(uint16_t *)(d + 2);
    uint16_t block_len = p->tot_len - 4;

    // Transition into DATA_RECV if we aren't already.
    tftp_state->state = TFTP_STATE_DATA_RECV;

    // Only receive in-order blocks.
    if (block == ((tftp_state->current_block + 1) & 0xFFFF)) {
      if ((tftp_state->data_offset + block_len) > tftp_state->data_maxlen) {
        tftp_state->state = TFTP_STATE_ERROR;
        tftp_state->error_msg = "Too much data received!";
        tftp_state->result = -1;
        break;
      }

      memcpy((uint8_t *)tftp_state->data + tftp_state->data_offset, d + 4,
             block_len);
      tftp_state->data_offset += block_len;
      tftp_state->current_block++;

      // TODO: Progress bar.
      if (!(tftp_state->current_block & 255)) {
        console_clrline();
        printf("%c %d packets @@ %d kb/s\r",
               "|/-\\"[(tftp_state->current_block >> 8) & 3],
               tftp_state->current_block,
               (int)(tftp_state->data_offset / 1024 * 1000 /
                     tb_diff_msec(mftb(), tftp_state->start_time)));
      }

      if (block_len != tftp_state->block_size) {
        tftp_state->state = TFTP_STATE_FINISH;
        tftp_state->result = 0;
      }

      send_ack(tftp_state->pcb, *addr, port, block);
    } else {
      console_clrline();
      printf("tftp: out of sequence block! (got %d, expected %d)\n", block,
             (tftp_state->current_block + 1) & 0xFFFF);
      if (block == (tftp_state->current_block & 0xFFFF)) {
        printf("dupe.\n");
        send_ack(tftp_state->pcb, *addr, port, block);
      }
    }
  } break;

  // Option acknowledgement.
  case TFTP_OPCODE_OACK: {
    if (!strcmp((char*)&d[2], "blksize")) {
      uint32_t blksize = strtol((char*)&d[10], NULL, 10);
      printf("tftp: server acknowledged blksize 0x%X\n", (int)blksize);

      // Use this block size from now on.
      tftp_state->block_size = blksize;
      tftp_state->state = TFTP_STATE_DATA_RECV;

      send_ack(tftp_state->pcb, *addr, port, 0);
    }
  } break;

  case TFTP_OPCODE_ERROR: {
    tftp_state->state = TFTP_STATE_FINISH;
    tftp_state->result = -2;

    /* please don't overflow this. */
    console_clrline();
    printf("tftp error %d: %s\n", (d[2] << 8) | d[3], d + 4);
  } break;

  default: {
    tftp_state->state = TFTP_STATE_ERROR;
    tftp_state->error_msg = "Unknown opcode.";
    tftp_state->result = -1;
  } break;
  }

  tftp_state->last_recv = mftb();
  tftp_state->tries = 0;
  pbuf_free(p);
}

int do_tftp(void *target, int maxlen, struct ip_addr server, const char *file) {
  int rc = 0;
  uint64_t start = mftb();

  tftp_state_t tftp_state;
  memset(&tftp_state, 0, sizeof(tftp_state));

  tftp_state.pcb = udp_new();
  if (!tftp_state.pcb) {
    printf("internal error: out of memory (udp)\n");
    return -1;
  }

  udp_bind(tftp_state.pcb, IP_ADDR_ANY, htons(0x1234));
  udp_recv(tftp_state.pcb, tftp_recv, &tftp_state);

  tftp_state.data = target;
  tftp_state.data_maxlen = maxlen;
  tftp_state.state = TFTP_STATE_RRQ_SEND;
  tftp_state.block_size = 512;
  tftp_state.start_time = mftb();
  tftp_state.last_recv = mftb();
  tftp_state.server_addr = server;
  tftp_state.server_port = 0;

  // Send the initial request.
  rc = send_rrq(tftp_state.pcb, server, 69, file, "octet", 1024);
  if (rc != 0) {
    tftp_state.state = TFTP_STATE_FINISH;
    tftp_state.result = -10;
  }

  while (tftp_state.state != TFTP_STATE_FINISH && tftp_state.state != TFTP_STATE_ERROR) {
    // Poll the network for any packets received.
    network_poll();

    uint64_t now = mftb();
    if (tb_diff_msec(now, tftp_state.last_recv) > 500) {
      // 500ms timeout. See what's up.
      if (tftp_state.tries >= TFTP_MAX_RETRIES) {
        printf("\nTFTP: timeout.\n");
        tftp_state.state = TFTP_STATE_ERROR;
        tftp_state.result = -5;
        tftp_state.error_msg = "timeout";
      }

      switch (tftp_state.state) {
        case TFTP_STATE_RRQ_SEND: {
          console_clrline();
          printf("TFTP: No answer from server.");
          tftp_state.state = TFTP_STATE_FINISH;
          tftp_state.result = -4;
        } break;

        case TFTP_STATE_DATA_RECV: {
          if (tftp_state.tries <= 1) {
            console_clrline();
            printf("TFTP: long delay between packets (%lums)...",
                  tb_diff_msec(now, tftp_state.last_recv));
            tftp_state.tries++;
          }
        } break;
      }
    }
  }

  if (tftp_state.state == TFTP_STATE_ERROR) {
    printf("TFTP error: %s\n", tftp_state.error_msg);
    
    if (tftp_state.server_port != 0) {
      // Notify the server.
      send_error(tftp_state.pcb, server, tftp_state.server_port, tftp_state.result, tftp_state.error_msg);
    }
  }

  if (tftp_state.result == 0) {
    uint64_t end = mftb();

    console_clrline();
    printf("%d packets (%d bytes, %d packet size), received in %dms, %d kb/s\n",
           tftp_state.current_block, tftp_state.data_offset,
           tftp_state.block_size, (int)tb_diff_msec(end, start),
           (int)(tftp_state.data_offset / 1024 * 1000 / tb_diff_msec(end, start)));
  }

  udp_remove(tftp_state.pcb);
  return (tftp_state.result < 0) ? tftp_state.result : tftp_state.data_offset;
}

int boot_tftp(ip_addr_t server_address, const char *tftp_bootfile,
              int filetype) {
  int ret;
  char *args = strchr(tftp_bootfile, ' ');
  if (args)
    *args++ = 0;

  // const char *msg = " was specified, neither manually nor via dhcp. aborting
  // tftp.\n";

  if (!server_address.addr) {
    console_clrline();
    printf("no tftp server address");
    // printf(msg);
    return -1;
  }

  if (!(tftp_bootfile && *tftp_bootfile)) {
    console_clrline();
    printf("no tftp bootfile name");
    // printf(msg);
    return -1;
  }
  // printf(" * loading tftp bootfile '%s:%s'\n", server_addr, tftp_bootfile);

  void *elf_raw = malloc(ELF_MAXSIZE);

  int res = do_tftp(elf_raw, ELF_MAXSIZE, server_address, tftp_bootfile);
  if (res < 0) {
    free(elf_raw);
    return res;
  }

  if (filetype == TYPE_ELF) {
    char *argv[] = {
        (char*)tftp_bootfile,
    };
    int argc = sizeof(argv) / sizeof(char *);

    elf_setArgcArgv(argc, argv);
  }

  ret = launch_file(elf_raw, res, filetype);

  free(elf_raw);
  return ret;
}

extern int boot_tftp_url(const char *url) {
  const char *bootfile = url;

  char server_addr[20];

  if (!bootfile)
    bootfile = "";

  // ip:/path
  // /path
  // ip

  const char *r = strchr(bootfile, ':');

  if (r) {
    int l = r - bootfile;
    if (l > 19)
      l = 19;
    memcpy(server_addr, bootfile, l);
    server_addr[l] = 0;
    bootfile = r + 1;
  } else {
    *server_addr = 0;
    bootfile = url;
  }

  ip_addr_t server_address;
  if (!ipaddr_aton(server_addr, &server_address)) {
    server_address.addr = 0;
  }

  return boot_tftp(server_address, bootfile, TYPE_ELF);
}

ip_addr_t boot_server_name() {
  ip_addr_t ret;
  if (kboot_tftp && kboot_tftp[0] && ipaddr_aton(kboot_tftp, &ret)) {
    return ret;
  }

  if (netif.dhcp) {
    // DHCP server.
    if (netif.dhcp->server_ip_addr.addr != 0x00000000) {
      return netif.dhcp->server_ip_addr;
    } else if (netif.gw.addr != 0x00000000) {
      return netif.gw;
    }
  }

  return ret;
}

char *boot_file_name() {
#if 0
	if (netif.dhcp && *netif.dhcp->boot_file_name)
		return netif.dhcp->boot_file_name;
#endif

  return "/tftpboot/xenon";
}
