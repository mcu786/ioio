#include <string.h>

#include "logging.h"
#include "flash.h"
#include "ioio_file.h"
#include "bootloader_defs.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))

typedef enum {
  IOIO_FILE_STATE_WAIT_HEADER,
  IOIO_FILE_STATE_WAIT_BLOCK
} IOIO_FILE_STATE;

// TODO: place all these in application data space
static BYTE ioio_file_buf[196];
static BYTE ioio_file_buf_pos;
static BYTE ioio_file_field_remaining;
static DWORD ioio_file_last_page;
static IOIO_FILE_STATE ioio_file_state;
static const BYTE IOIO_FILE_HEADER[8] = { 'I', 'O', 'I', 'O', '\1', '\0', '\0', '\0' };

static BOOL IOIOFileSectionDone() {
  DWORD address, page_address;
  switch (ioio_file_state) {
    case IOIO_FILE_STATE_WAIT_HEADER:
      if (memcmp(ioio_file_buf, IOIO_FILE_HEADER, 8) != 0) {
        log_print_0("Unexpected IOIO file header or version");
        return FALSE;
      }
      ioio_file_buf_pos = 0;
      ioio_file_field_remaining = 196;
      ioio_file_state = IOIO_FILE_STATE_WAIT_BLOCK;
      break;

    case IOIO_FILE_STATE_WAIT_BLOCK:
      address = *((const DWORD *) ioio_file_buf);
      if (address & 0x7F) {
        log_print_1("Misaligned block: 0x%lx", address);
        return FALSE;
      }
      if (address < BOOTLOADER_MIN_ADDRESS || address >= BOOTLOADER_MAX_ADDRESS) {
        log_print_1("Adderess outside of permitted range: 0x%lx", address);
        return FALSE;
      }
      page_address = address & 0xFFFFFFC00ull;
      if (page_address != ioio_file_last_page) {
        FlashErasePage(page_address);
        ioio_file_last_page = page_address;
      }
      FlashWriteBlock(address, ioio_file_buf + 8);
      ioio_file_buf_pos = 0;
      ioio_file_field_remaining = 196;
      break;
  }
  return TRUE;
}

void IOIOFileInit() {
  ioio_file_buf_pos = 0;
  ioio_file_field_remaining = 8;
  ioio_file_last_page = BOOTLOADER_INVALID_ADDRESS;
  ioio_file_state = IOIO_FILE_STATE_WAIT_HEADER;
}

BOOL IOIOFileHandleBuffer(const void * buffer, size_t size) {
  while (size) {
    size_t bytes_to_copy = MIN(ioio_file_field_remaining, size);
    memcpy(ioio_file_buf + ioio_file_buf_pos, buffer, bytes_to_copy);
    ioio_file_buf_pos += bytes_to_copy;
    ioio_file_field_remaining -= bytes_to_copy;
    buffer = ((const BYTE *) buffer) + bytes_to_copy;
    size -= bytes_to_copy;
    if (ioio_file_field_remaining == 0) {
      if (!IOIOFileSectionDone()) {
        return FALSE;
      }
    }
  }
  return TRUE;
}

BOOL IOIOFileDone() {
  if (ioio_file_state == IOIO_FILE_STATE_WAIT_BLOCK
      && ioio_file_field_remaining == 196) return TRUE;
  log_print_0("Unexpected EOF");
  return FALSE;
}