/* tar.c

   Creates a tar archive. */

#include <ustar.h>
#include <syscall.h>
#include <stdio.h>
#include <string.h>

static void usage (void);
static bool make_tar_archive (const char *archive_name,
                              char *files[], size_t file_cnt);

int
main (int argc, char *argv[]) 
{
  printf("tar is starting!!!!! \n");
  if (argc < 3)
    usage ();

  bool result=(make_tar_archive (argv[1], argv + 2, argc - 2)
          ? EXIT_SUCCESS : EXIT_FAILURE);
  //bool result= true;
  if(result==EXIT_SUCCESS)
    printf("tar success\n");
  else
    printf("tar failed!\n");

  return result;
}

static void
usage (void) 
{
  printf ("tar, tar archive creator\n"
          "Usage: tar ARCHIVE FILE...\n"
          "where ARCHIVE is the tar archive to create\n"
          "  and FILE... is a list of files or directories to put into it.\n"
          "(ARCHIVE itself will not be included in the archive, even if it\n"
          "is in a directory to be archived.)\n");
  exit (EXIT_FAILURE);
}

static bool archive_file (char file_name[], size_t file_name_size,
                          int archive_fd, bool *write_error);

static bool archive_ordinary_file (const char *file_name, int file_fd,
                                   int archive_fd, bool *write_error);
static bool archive_directory (char file_name[], size_t file_name_size,
                               int file_fd, int archive_fd, bool *write_error);
static bool write_header (const char *file_name, enum ustar_type, int size,
                          int archive_fd, bool *write_error);

static bool do_write (int fd, const char *buffer, int size, bool *write_error);

static bool
make_tar_archive (const char *archive_name, char *files[], size_t file_cnt) 
{
  printf("archive_name %s\n",archive_name );
  printf("there are %d files we want\n", file_cnt );
  printf("at the beginning\n");
  static const char zeros[512];
  int archive_fd;
  bool success = true;
  bool write_error = false;
  size_t i;
  
  if (!create (archive_name, 0)) 
    {
      printf ("%s: create failed\n", archive_name);
      return false;
    }
  archive_fd = open (archive_name);
  if (archive_fd < 0)
    {
      printf ("%s: open failed\n", archive_name);
      return false;
    }
  printf("half way1\n");

  for (i = 0; i < file_cnt; i++) 
    {
      printf("file name %s\n",files[i] );
      char file_name[128];
      
      strlcpy (file_name, files[i], sizeof file_name);
      if (!archive_file (file_name, sizeof file_name,
                         archive_fd, &write_error))
        success = false;
    }

  if(success)
    printf("half way 2 success\n");  
  else
    printf("half way 2 failed!\n");


  if (!do_write (archive_fd, zeros, 512, &write_error)
      || !do_write (archive_fd, zeros, 512, &write_error)) 
    success = false;

  if(success)
    printf("half way 3 success\n");  
  else
    printf("half way 3 failed!\n");

  close (archive_fd);
  printf("we are done!!\n");
  return success;
}

static bool
archive_file (char file_name[], size_t file_name_size,
              int archive_fd, bool *write_error) 
{
  printf("a1!\n");

  int file_fd = open (file_name);

  printf("a2!\n");

  if (file_fd >= 0) 
    {
      bool success;

      printf("a22!\n");

      if (inumber (file_fd) != inumber (archive_fd)) 
        {
          printf("a231!\n");

          if (!isdir (file_fd))
          {
            success = archive_ordinary_file (file_name, file_fd,
                                             archive_fd, write_error);
            //success=true;
            //printf("I am trying not to archive file\n");
            printf("do nothing for file\n");
          }
            

          else{

            success = archive_directory (file_name, file_name_size, file_fd,
                                         archive_fd, write_error);
            //success=true;
            //printf("do nothing for directory\n");
          }

          printf("a24!\n");
                                     
        }
      else
        {
          printf("a230!\n");
          /* Nothing to do: don't try to archive the archive file. */
          success = true;
        }
    printf("a3!\n");

      close (file_fd);

    printf("a4!\n");

      return success;
    }
  else
    {
      printf ("%s: open failed\n", file_name);
      return false;
    }
}

static bool
archive_ordinary_file (const char *file_name, int file_fd,
                       int archive_fd, bool *write_error)
{
  printf("archive regular file %s\n", file_name );
  printf("ao 1\n");

  bool read_error = false;
  bool success = true;
  int file_size = filesize (file_fd);

  if (!write_header (file_name, USTAR_REGULAR, file_size,
                     archive_fd, write_error))
    return false;
  printf("ao 2\n");

  while (file_size > 0) 
    {
      static char buf[512];
      int chunk_size = file_size > 512 ? 512 : file_size;
      int read_retval = read (file_fd, buf, chunk_size);
      int bytes_read = read_retval > 0 ? read_retval : 0;

      if (bytes_read != chunk_size && !read_error) 
        {
          printf ("%s: read error\n", file_name);
          read_error = true;
          success = false;
        }

      memset (buf + bytes_read, 0, 512 - bytes_read);
      if (!do_write (archive_fd, buf, 512, write_error))
        success = false;

      file_size -= chunk_size;
    }
  printf("ao 3\n");
  
  return success;
}

static bool
archive_directory (char file_name[], size_t file_name_size, int file_fd,
                   int archive_fd, bool *write_error)
{
  printf("archive directory name %s\n", file_name);
  size_t dir_len;
  bool success = true;
  printf("ad 1\n");
  dir_len = strlen (file_name);
  if (dir_len + 1 + READDIR_MAX_LEN + 1 > file_name_size) 
    {
      printf ("%s: file name too long\n", file_name);
      return false;
    }
  printf("ad 2\n");
  
  //if (!write_header (file_name, USTAR_DIRECTORY, 0, archive_fd, write_error))
  //  return false;
  printf("ad 3\n");
      
  file_name[dir_len] = '/';
  while (readdir (file_fd, &file_name[dir_len + 1])) 
    if (!archive_file (file_name, file_name_size, archive_fd, write_error))
      success = false;
  file_name[dir_len] = '\0';
  printf("ad 4\n");

  return success;
}

static bool
write_header (const char *file_name, enum ustar_type type, int size,
              int archive_fd, bool *write_error) 
{
  static char header[512];
  return (ustar_make_header (file_name, type, size, header)
          && do_write (archive_fd, header, 512, write_error));
}

static bool
do_write (int fd, const char *buffer, int size, bool *write_error) 
{
  if (write (fd, buffer, size) == size) 
    return true;
  else
    {
      if (!*write_error) 
        {
          printf ("error writing archive\n");
          *write_error = true; 
        }
      return false; 
    }
}
