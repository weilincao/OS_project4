#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"


#define NUMBER_OF_DIRECT_POINTERS 120
#define NUMBER_OF_INDIRECT_POINTERS 4
#define NUMBER_OF_DOUBLE_INDIRECT_POINTERS 1
#define NUMBER_OF_POINTERS_PER_INDIRECT_POINTER_TABLE 128 //512/4

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44


/*
self-explanatory
*/
struct indirect_table{
  uint32_t indirect_table_entries[NUMBER_OF_POINTERS_PER_INDIRECT_POINTER_TABLE];
};


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    //block_sector_t start;               /* First data sector. */

    uint32_t direct_ptr[NUMBER_OF_DIRECT_POINTERS];
    uint32_t indirect_ptr[NUMBER_OF_INDIRECT_POINTERS];
    uint32_t double_indirect_ptr;

    bool is_dir;
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */


    //new
    struct lock lock_inode;

  };

/*
grow the size of the given inode located in disk to the given new size
the new portion that is grown are initialized with zeros.

return 1 if the process is successful, -1 otherwise 

note: right now, double indirect pointer is not yet implemented

*/
int grow_file_size(struct inode_disk *disk_inode, off_t new_size)
{
  static char zeros[BLOCK_SECTOR_SIZE];

  if (new_size< 0)
  {
    printf("Oh no, the given size is negative\n");
    return -1;
  }




  int num_of_sectors = bytes_to_sectors(new_size);

  //divide the number of sectors into three section:
  int num_of_sectors_for_direct_ptr=0;
  int num_of_sectors_for_indirect_ptr=0;
  int num_of_sectors_for_double_indirect_ptr=0;

  // first detemine number of sectors need to be allocated for each of the 3 types of pointers
  if(num_of_sectors> NUMBER_OF_DIRECT_POINTERS)
  {
    num_of_sectors_for_direct_ptr=NUMBER_OF_DIRECT_POINTERS;

    if(   (num_of_sectors-NUMBER_OF_DIRECT_POINTERS) <  (NUMBER_OF_INDIRECT_POINTERS*NUMBER_OF_POINTERS_PER_INDIRECT_POINTER_TABLE)   )
    {
      num_of_sectors_for_indirect_ptr=num_of_sectors-NUMBER_OF_DIRECT_POINTERS;

    }
    else
    {
      printf("double pointer are used\n");
      num_of_sectors_for_indirect_ptr=NUMBER_OF_INDIRECT_POINTERS*NUMBER_OF_POINTERS_PER_INDIRECT_POINTER_TABLE;
      num_of_sectors_for_double_indirect_ptr=num_of_sectors-NUMBER_OF_DIRECT_POINTERS-num_of_sectors_for_indirect_ptr;
    }
  }
  else
  {
    num_of_sectors_for_direct_ptr=num_of_sectors;
  }

  //direct pointer allocation:
  for(int i =0; i< num_of_sectors_for_direct_ptr; i++)
  {
    //printf("direct_ptr is %d\n", disk_inode->direct_ptr[i]);
    if(disk_inode->direct_ptr[i]==0)//if the sector is not yet allocated
    {
      if(! free_map_allocate (1, &disk_inode->direct_ptr[i])){
        printf("inode bitmap allocate failed during grow file size1\n");
        return -1;
      }
      block_write (fs_device, disk_inode->direct_ptr[i], zeros);
    }
  }


  //now the indirect pointer allocation:
  struct indirect_table* table_buffer= malloc(sizeof(struct indirect_table));;

  for(int i =0 ; i<num_of_sectors_for_indirect_ptr; i++)
  {
    //printf("indirect ptr!!!\n");
    int index_indirect_ptr=i/NUMBER_OF_POINTERS_PER_INDIRECT_POINTER_TABLE;
    int index_indirect_ptr_table=i%NUMBER_OF_POINTERS_PER_INDIRECT_POINTER_TABLE;


    //allocate a sector if the current indirect pointer is empty
    if(disk_inode->indirect_ptr[index_indirect_ptr]==NULL)
    {
      if(! free_map_allocate (1, &disk_inode->indirect_ptr[index_indirect_ptr]))
      {
      printf("inode bitmap allocate failed during grow file size2\n");
      return -1;
      }    
      block_write (fs_device, disk_inode->indirect_ptr[index_indirect_ptr], zeros);

    }

    //load the indirect table from disk to the table buffer whenever the loop reach the beginning of the the table
    if(index_indirect_ptr_table==0)
    {
       block_read (fs_device, disk_inode->indirect_ptr[index_indirect_ptr], table_buffer);
    }

    //allocate a sector if the current table entries is empty
    if(table_buffer->indirect_table_entries[index_indirect_ptr_table]==NULL)
    {
      if(! free_map_allocate (1, &table_buffer->indirect_table_entries[index_indirect_ptr_table]))
      {
      printf("inode bitmap allocate failed during grow file size3\n");
      return -1;
      }    
      block_write (fs_device, table_buffer->indirect_table_entries[index_indirect_ptr_table], zeros);
    }

    //whenever the loop is going to end or the buffer table reaches the end, write back to the actual disk
    if(index_indirect_ptr_table==NUMBER_OF_POINTERS_PER_INDIRECT_POINTER_TABLE-1 || index_indirect_ptr_table == num_of_sectors_for_indirect_ptr-1 )
    {
      block_write (fs_device, disk_inode->indirect_ptr[index_indirect_ptr], table_buffer);
    }



  }
  free(table_buffer);

  disk_inode->length=new_size;
  return 1;

  //indirect table is already a total mess, so I am not going to implement double indirect ptr for now
}




/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
/*
  modified for growing file size, however, currently, lock mechanism is needed,
  at the same time, double indirect pointer is no yet implment.
*/
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
  {
    
    if(pos< (BLOCK_SECTOR_SIZE*NUMBER_OF_DIRECT_POINTERS) )
      return inode->data.direct_ptr[pos / BLOCK_SECTOR_SIZE];
    else if(pos < (BLOCK_SECTOR_SIZE*NUMBER_OF_DIRECT_POINTERS+NUMBER_OF_INDIRECT_POINTERS*NUMBER_OF_POINTERS_PER_INDIRECT_POINTER_TABLE*BLOCK_SECTOR_SIZE ) )
    {
      int num_of_sectors_for_indirect_ptr= (pos-NUMBER_OF_DIRECT_POINTERS*BLOCK_SECTOR_SIZE)/BLOCK_SECTOR_SIZE;
      int index_indirect_ptr= num_of_sectors_for_indirect_ptr/BLOCK_SECTOR_SIZE;
      int index_indirect_ptr_table=num_of_sectors_for_indirect_ptr%BLOCK_SECTOR_SIZE;

      struct indirect_table *table_buffer = malloc(sizeof(struct indirect_table));
      block_read(fs_device, inode->data.indirect_ptr[index_indirect_ptr], table_buffer);
      uint32_t return_sector = table_buffer->indirect_table_entries[index_indirect_ptr_table];
      free(table_buffer);
      return return_sector;

    }
    else
    {
      printf("oh no! you need to use double indirect pointer, however, we have not implement it yet!\n"); 
      return -2;

    }
  }
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length,bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir=is_dir;
      if (grow_file_size (disk_inode, length) )
        {
          block_write (fs_device, sector, disk_inode);//writing the inode
          success = true;
        }

      free (disk_inode);
    }

  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);


  lock_init (&inode->lock_inode);

  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}


void free_map_release_disk_inode(struct inode_disk* disk_inode)
{
  size_t num_of_sectors = bytes_to_sectors(disk_inode->length);  // number of sectors to be allocated (will decrease as we allocate)

  // direct pointers
  int n = num_of_sectors < NUMBER_OF_DIRECT_POINTERS ? num_of_sectors : NUMBER_OF_DIRECT_POINTERS;
  for (int i=0; i < n; i++)
    free_map_release (disk_inode->direct_ptr[i], 1);
  num_of_sectors -= n;
  if (num_of_sectors == 0) return;

  //need to implement indirect pointer and double indirect pointer
  return;
}



/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
/*
  this function is modify for growing file size, however,
  I have not add deallocation mechanism in it yet.
*/
void
inode_close (struct inode *inode)
{
  //printf("closing inode!\n"); 
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  //the open counter need to decrease
  //lock_acquire (&inode->lock_inode);
  inode->open_cnt--;
  //lock_release (&inode->lock_inode);

  /* Release resources if this was the last opener. */
  if (inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          free_map_release_disk_inode(&inode->data);
          //free_map_release (inode->data.start,
          //                  bytes_to_sectors (inode->data.length));
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */

/*
  this method has been modified for growing file size

  !!!notice: currently, there is no synchronization mechanism
*/
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;

      if (chunk_size <= 0)//start allocating if the inode length is less than offset
      {
        if (!grow_file_size(&inode->data, offset+size)) 
        {
            printf("fail to grow the file during write!\n");
            return -1;
        }
        else
        {
          //bool lock_held = lock_held_by_current_thread (&inode->lock_inode);
          //if (!lock_held) lock_acquire(&inode->lock_inode);
          inode->data.length = offset + size;
          //if (!lock_held) lock_release(&inode->lock_inode);
          block_write (fs_device, inode->sector, &inode->data);  
          continue; // now the allocated space is enough, redo the loop.
        }
      }

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

bool is_inode_dir(struct inode* inode)
{
  return inode->data.is_dir;
}

int inode_open_cnt(struct inode* inode)
{
  return inode->open_cnt;
}