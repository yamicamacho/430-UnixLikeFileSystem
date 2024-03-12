// ============================================================================
// fs.c - user FileSytem API
// ============================================================================

#include "fs.h"
#include "bfs.h"

// ============================================================================
// Close the file currently open on file descriptor 'fd'.
// ============================================================================
i32 fsClose(i32 fd) {
  i32 inum = bfsFdToInum(fd);
  bfsDerefOFT(inum);
  return 0;
}

// ============================================================================
// Create the file called 'fname'.  Overwrite, if it already exsists.
// On success, return its file descriptor.  On failure, EFNF
// ============================================================================
i32 fsCreate(str fname) {
  i32 inum = bfsCreateFile(fname);
  if (inum == EFNF)
    return EFNF;
  return bfsInumToFd(inum);
}

// ============================================================================
// Format the BFS disk by initializing the SuperBlock, Inodes, Directory and
// Freelist.  On succes, return 0.  On failure, abort
// ============================================================================
i32 fsFormat() {
  FILE *fp = fopen(BFSDISK, "w+b");
  if (fp == NULL)
    FATAL(EDISKCREATE);

  i32 ret = bfsInitSuper(fp); // initialize Super block
  if (ret != 0) {
    fclose(fp);
    FATAL(ret);
  }

  ret = bfsInitInodes(fp); // initialize Inodes block
  if (ret != 0) {
    fclose(fp);
    FATAL(ret);
  }

  ret = bfsInitDir(fp); // initialize Dir block
  if (ret != 0) {
    fclose(fp);
    FATAL(ret);
  }

  ret = bfsInitFreeList(); // initialize Freelist
  if (ret != 0) {
    fclose(fp);
    FATAL(ret);
  }

  fclose(fp);
  return 0;
}

// ============================================================================
// Mount the BFS disk.  It must already exist
// ============================================================================
i32 fsMount() {
  FILE *fp = fopen(BFSDISK, "rb");
  if (fp == NULL)
    FATAL(ENODISK); // BFSDISK not found
  fclose(fp);
  return 0;
}

// ============================================================================
// Open the existing file called 'fname'.  On success, return its file
// descriptor.  On failure, return EFNF
// ============================================================================
i32 fsOpen(str fname) {
  i32 inum = bfsLookupFile(fname); // lookup 'fname' in Directory
  if (inum == EFNF)
    return EFNF;
  return bfsInumToFd(inum);
}

// ============================================================================
// Read 'numb' bytes of data from the cursor in the file currently fsOpen'd on
// File Descriptor 'fd' into 'buf'.  On success, return actual number of bytes
// read (may be less than 'numb' if we hit EOF).  On failure, abort
// ============================================================================
i32 fsRead(i32 fd, i32 numb, void *buf) {
  // bfsFdToInum -- get inum
  i32 inum = bfsFdToInum(fd);
  // fsTell - to get cursor location
  i32 cursor = bfsTell(fd);
  // fbnStart = curs / 512 (BYTESPERBLOCK)
  i32 fbnStart = cursor / BYTESPERBLOCK;
  // fbnEnd = (curs + numb) / 512
  i32 fbnEnd = (cursor + numb) / BYTESPERBLOCK;

  i32 readBytes = 0;

  for (i32 fbn = fbnStart; fbn <= fbnEnd; fbn++) { // loop

    // bfsFbnToDbn -- file block number to disk block number
    i32 dbn = bfsFbnToDbn(inum, fbn);

    // Buffer to hold block data
    i8 bio[BYTESPERBLOCK];

    // Read data from disk block
    // bioRead -- on disk block number
    bioRead(dbn, bio);

    // Calculate the offset
    i32 offset;
    if (fbn == fbnStart) {
      offset = cursor % BYTESPERBLOCK;
    } else {
      offset = 0;
    }

    // Calculate the number of bytes to read from this block
    i32 bytesToRead;
    if (fbn == fbnEnd) {
      bytesToRead = (cursor + numb) % BYTESPERBLOCK - offset;
    } else {
      bytesToRead = BYTESPERBLOCK - offset;
    }

    // memcpy - to buf from 512 bytes read
    memcpy(buf + readBytes, bio + offset, bytesToRead);

    // Update cursor position, number of bytes read, and remaining bytes to read
    cursor += bytesToRead;
    readBytes += bytesToRead;

    // advance our cursor
    // bfsSetCursor -- advance cursor
    bfsSetCursor(inum, cursor);
  } // end loop

  return readBytes;
}

// ============================================================================
// Move the cursor for the file currently open on File Descriptor 'fd' to the
// byte-offset 'offset'.  'whence' can be any of:
//
//  SEEK_SET : set cursor to 'offset'
//  SEEK_CUR : add 'offset' to the current cursor
//  SEEK_END : add 'offset' to the size of the file
//
// On success, return 0.  On failure, abort
// ============================================================================
i32 fsSeek(i32 fd, i32 offset, i32 whence) {

  if (offset < 0)
    FATAL(EBADCURS);

  i32 inum = bfsFdToInum(fd);
  i32 ofte = bfsFindOFTE(inum);

  switch (whence) {
  case SEEK_SET:
    g_oft[ofte].curs = offset;
    break;
  case SEEK_CUR:
    g_oft[ofte].curs += offset;
    break;
  case SEEK_END: {
    i32 end = fsSize(fd);
    g_oft[ofte].curs = end + offset;
    break;
  }
  default:
    FATAL(EBADWHENCE);
  }
  return 0;
}

// ============================================================================
// Return the cursor position for the file open on File Descriptor 'fd'
// ============================================================================
i32 fsTell(i32 fd) { return bfsTell(fd); }

// ============================================================================
// Retrieve the current file size in bytes.  This depends on the highest offset
// written to the file, or the highest offset set with the fsSeek function.  On
// success, return the file size.  On failure, abort
// ============================================================================
i32 fsSize(i32 fd) {
  i32 inum = bfsFdToInum(fd);
  return bfsGetSize(inum);
}

// ============================================================================
// Write 'numb' bytes of data from 'buf' into the file currently fsOpen'd on
// filedescriptor 'fd'.  The write starts at the current file offset for the
// destination file.  On success, return 0.  On failure, abort
// ============================================================================
i32 fsWrite(i32 fd, i32 numb, void *buf) {
  i32 inum = bfsFdToInum(fd);
  i32 cursor = bfsTell(fd);
  i32 fbnStart = cursor / BYTESPERBLOCK;
  i32 fbnEnd = (cursor + numb) / BYTESPERBLOCK;
  i8 bio[BYTESPERBLOCK];
  i32 bytesToWrite = numb;
  i32 bioOffset = ((fbnStart + 1) * BYTESPERBLOCK) - cursor;
  i32 bufOffset = 0;
  i32 dbnNum;

  

  for (i32 i = fbnStart; i <= fbnEnd; i++) {
    if (bytesToWrite == 0) {
      break;
    }

    dbnNum = bfsFbnToDbn(inum, i);

    if (bioOffset > 0) {
      bioRead(dbnNum, bio);
      i32 writing = (bioOffset >= bytesToWrite) ? bytesToWrite : bioOffset;
      memcpy((bio + (BYTESPERBLOCK - bioOffset)), (buf + bufOffset), writing);
      bioWrite(dbnNum, bio);
      bufOffset += writing;
      bytesToWrite -= writing;
      bioOffset = 0;
    } else {
      i32 writing =
          (BYTESPERBLOCK >= bytesToWrite) ? bytesToWrite : BYTESPERBLOCK;
      bioRead(dbnNum, bio);
      memcpy(bio, (buf + bufOffset), writing);
      bioWrite(dbnNum, bio);
      bufOffset += writing;
      bytesToWrite -= writing;
    }
  }

  bfsSetCursor(inum, cursor + numb);
  return 0;
}
