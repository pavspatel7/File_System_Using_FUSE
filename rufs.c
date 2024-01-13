/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

// Students Name: Pavitra Patel (php51), Kush Patel (kp1085)
// Code tested on rlab2.cs.rutgers.edu and our VM cs416f23-28

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "rufs.h"

#include <math.h>

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here
struct superblock *sb;
bitmap_t inode_bitmap;
bitmap_t datablock_bitmap;
int debugging = 1;
void *temp_block;

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

    // Step 1: Read inode bitmap from disk
    // skip this step because my inode_bitmap is global which is ready to use and up to date
    
    // Step 2: Traverse inode bitmap to find an available slot
    if(debugging == 1)
    {
        puts("\nentered get_avail_ino");
        fflush(stdout);
    }

    int ino = 0;
    while(ino < sb->max_inum && get_bitmap(inode_bitmap, ino) != 0)
	{
		ino++;
	}

    // Step 3: Update inode bitmap and write to disk 
    if(ino < sb->max_inum) {
        set_bitmap(inode_bitmap, ino);
        // bio_write(sb->i_bitmap_blk, inode_bitmap);
        if(debugging == 1)
        {
            printf("exited get_avail_ino, inode found: %d\n", ino);
            fflush(stdout);
        }
        return ino;
    }

    if(debugging == 1)
    {
        puts("exited get_avail_ino: no inode found\n");
        fflush(stdout);
    }

    // No available inode found
    return -1;
}


/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

    // Step 1: Read data block bitmap from disk
    // skip this step because my datablock_bitmap is global which is ready to use and up to date

    // Step 2: Traverse data block bitmap to find an available slot

    if(debugging == 1)
    {
        puts("\nentered get_avail_blkno");
        fflush(stdout);
    }

    int dno = 0;
    while(dno < sb->max_dnum && get_bitmap(datablock_bitmap, dno) != 0)
	{
		dno++;
	}

    // Step 3: Update data block bitmap and write to disk 
    if(dno < sb->max_dnum) {
        set_bitmap(datablock_bitmap, dno);
        // bio_write(sb->d_bitmap_blk, datablock_bitmap);
        if(debugging == 1)
        {
            printf("exited get_avail_blkno, data block found: %d\n", sb->d_start_blk+dno);
            fflush(stdout);
        }
        return sb->d_start_blk+dno;
    }

    if(debugging == 1)
    {
        puts("exited get_avail_blkno: no data block found\n");
        fflush(stdout);
    }
    // No available data block found
    return -1;
}


/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

    if(debugging == 1)
    {
        puts("\nentered readi");
        fflush(stdout);
    }

	// Step 1: Get the inode's on-disk block number
	int block_number = 3 + (ino / (BLOCK_SIZE / sizeof(struct inode)));

	// Step 2: Get offset of the inode in the inode on-disk block
    memset(temp_block, 0, BLOCK_SIZE);
	if(bio_read(block_number, temp_block) <= 0)
		return -1;

	int offset_within_block = (ino % (BLOCK_SIZE / sizeof(struct inode)))*(sizeof(struct inode));

	// Step 3: Read the block from disk and then copy into inode structure
	memcpy(inode, (char *)temp_block+offset_within_block, sizeof(struct inode));

    if(debugging == 1)
    {
        puts("exited readi\n");
        fflush(stdout);
    }

	return 0;
}


int writei(uint16_t ino, struct inode *inode) {

    if(debugging == 1)
    {
        puts("\nentered writei");
        fflush(stdout);
    }

	// Step 1: Get the block number where this inode resides on disk
	int block_number = 3 + (ino / (BLOCK_SIZE / sizeof(struct inode)));
	
	// Step 2: Get the offset in the block where this inode resides on disk
    memset(temp_block, 0, BLOCK_SIZE);
	if(bio_read(block_number, temp_block) <= 0)
		return -1;
	int offset_within_block = (ino % (BLOCK_SIZE / sizeof(struct inode)))*(sizeof(struct inode));

	// Step 3: Write inode to disk 
	memcpy((char *)temp_block+offset_within_block, inode, sizeof(struct inode));
	if(bio_write(block_number, temp_block) <= 0)
		return -1;

    if(debugging == 1)
    {
        puts("exited writei\n");
        fflush(stdout);
    }

	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

    if(debugging == 1)
    {
        puts("\nentered dir_find");
        fflush(stdout);
    }

    // Step 1: Call readi() to get the inode using ino (inode number of current directory)
    struct inode target_inode;
    if (readi(ino, &target_inode) != 0)
        return -1;

    // handling direct pointers
    for (int i = 0; i < 16; i++) {
        if (target_inode.direct_ptr[i] != -1) {
            int index = target_inode.direct_ptr[i];
            memset(temp_block, 0, BLOCK_SIZE);
            if (bio_read(index, temp_block) <= 0) {
                return -1;
            }
            struct dirent *entries = (struct dirent *)temp_block;
            for (int j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++) {
                if (entries[j].valid != 0 && strncmp(entries[j].name, fname, name_len) == 0  && entries[j].len == name_len) {
                    // Found the desired entry
                    memcpy(dirent, &entries[j], sizeof(struct dirent));
                    return 0;
                }
            }
        }
    }

    // handling indirect pointers
    for (int i = 0; i < 8; i++) {
        if (target_inode.indirect_ptr[i] != -1) {
            int index = target_inode.indirect_ptr[i];
            memset(temp_block, 0, BLOCK_SIZE);
            if (bio_read(index, temp_block) <= 0) {
                return -1;
            }

            int *entries = (int *)temp_block;
            for (int j = 0; j < BLOCK_SIZE / sizeof(int); j++) {
                if (entries[j] != 0) {
                    void *block = malloc(BLOCK_SIZE);
                    memset(block, 0, BLOCK_SIZE);
                    if (bio_read(entries[j], block) <= 0) {
                        free(block);
                        return -1;
                    }

                    struct dirent *entries1 = (struct dirent *)block;
                    for (int k = 0; k < BLOCK_SIZE / sizeof(struct dirent); k++) {
                        if (entries1[k].valid != 0 && strncmp(entries1[k].name, fname, name_len) == 0  && entries1[k].len == name_len) {
                            // Found the desired entry
                            memcpy(dirent, &entries1[k], sizeof(struct dirent));
                            free(block);
                            return 0;
                        }
                    }
                    free(block);
                }
            }
        }
    }

    if(debugging == 1)
    {
        puts("exited dir_find\n");
        fflush(stdout);
    }

    // Entry not found
    return -1;
}


int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

    if(debugging == 1)
    {
        puts("\nentered dir_add\n");
        fflush(stdout);
    }

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	// Step 2: Check if fname (directory name) is already used in other entries
	// We will use dir_find to do step 1 and step 2
	struct dirent existing_entry;
	if(dir_find(dir_inode.ino, fname, name_len, &existing_entry) == 0)
	{
		// Directory name with such name already exists
		return -1;
	}

	// Step 3: Add directory entry in dir_inode's data block and write to disk
	// Allocate a new data block for this directory if it does not exist
	// Update directory inode
	// Write directory entry
	
	// To do step three we will have to consider conditions for both direct and indirect pointers
	// First considering direct pointers
	for(int i=0; i<16; i++)
	{
		if(dir_inode.direct_ptr[i] == -1)
		{
			// If block not allocated, then allocate
			// write to block, do necessary updates, and return success
			
			// find next available block
			int new_block = get_avail_blkno();
			if(new_block == -1)
			{
				return -1;
			}
			// update where the director pointer will point after allocation
			dir_inode.direct_ptr[i] = new_block;
			
            // now we need to update inode information as a result on disk
            time_t current_time = time(NULL);
            dir_inode.vstat.st_atime = current_time;
            dir_inode.vstat.st_mtime = current_time;
            dir_inode.size += sizeof(struct dirent);
            if (writei(dir_inode.ino, &dir_inode) != 0)
                return -1;
			
            // initialize the new data block
        	memset(temp_block, 0, BLOCK_SIZE);

			// add dirent to new data block
			struct dirent *new_entries = (struct dirent *)temp_block;
			new_entries[0].valid = 1;
			new_entries[0].ino = f_ino;
			strncpy(new_entries[0].name, fname, name_len);
			new_entries[0].name[name_len] = '\0';
            new_entries[0].len = name_len;

			// write the new data block to the disk
			if(bio_write(new_block, temp_block) <= 0)
			{
        	    return -1;
	        }
			return 0;
		}
		else
		{
			// If the block is allocated check if there is any space in block to fit a dirent
			// write to block, do necessary updates, and return success

			// only do this if i<16
			if(i < 16)
			{
                // get the data block index
				int new_block = dir_inode.direct_ptr[i];
				memset(temp_block, 0, BLOCK_SIZE);
				if(bio_read(new_block, temp_block) <= 0)
				{
					return -1;
				}
				struct dirent *entries = (struct dirent *)temp_block;
				for(int j=0; j<BLOCK_SIZE/sizeof(struct dirent); j++)
				{
					if(entries[j].valid == 0)
					{
                        // update indoe info on the disk
                        time_t current_time = time(NULL);
                        dir_inode.vstat.st_atime = current_time;
                        dir_inode.vstat.st_mtime = current_time;
                        dir_inode.size += sizeof(struct dirent);
                        if (writei(dir_inode.ino, &dir_inode) != 0)
                            return -1;

                        memset(temp_block, 0, BLOCK_SIZE);
                        bio_read(new_block, temp_block);

                        // create and add dirent to the new data block
						entries[j].valid = 1;
						entries[j].ino = f_ino;
						strncpy(entries[j].name, fname, name_len);
						entries[j].name[name_len] = '\0';
                        entries[j].len = name_len;

						// Write the updated data block to disk
						if(bio_write(new_block, temp_block) <= 0)
						{
							return -1;
						}
						return 0;
					}
				}
			}
		}
	}

	// Now considering indirect pointers
	if(dir_inode.direct_ptr[15] != -1)
	{
		for(int i=0; i<8; i++)
		{
			if(dir_inode.indirect_ptr[i] == -1)
			{
				// first, allocate a new block where we will store entries of additional blocks using get_avail_blockno.
				// Use get_avail_blockno. again to find a block where we will store our dirent
				// write both data blocks back to disk
				// write inode back to disk
				
				// first, allocate a new block where we will store entries of additional blocks using get_avail_blockno.
				int new_indirect_block = get_avail_blkno();
				if(new_indirect_block == -1)
					return -1;
				// second, allocate a new block where we will store our dirent
				int new_data_block = get_avail_blkno();
				if(new_data_block == -1)
					return -1;
				// update the new_indirect_block with the new_data_block
				void *indirect_block_data = malloc(BLOCK_SIZE);
				memset(indirect_block_data, 0, BLOCK_SIZE);
				if(bio_read(new_indirect_block, indirect_block_data) <= 0)
				{
					free(indirect_block_data);
					return -1;
				}
				int *indirect_entries = (int *)indirect_block_data;
				indirect_entries[0] = new_data_block;
				// write the indirect pointer data block back to disk
				if(bio_write(new_indirect_block, indirect_block_data) <= 0)
				{
					free(indirect_block_data);
					return -1;
				}
				free(indirect_block_data);
				// third, update the inode with the new indirect block
				dir_inode.indirect_ptr[i] = new_indirect_block;
				// Now we need to update the inode info on the disk
				time_t current_time = time(NULL);
                dir_inode.vstat.st_atime = current_time;
                dir_inode.vstat.st_mtime = current_time;
                dir_inode.size += sizeof(struct dirent);
                if(writei(dir_inode.ino, &dir_inode) != 0)
					return -1;
				// Fourth, initialize the new data block
				void *new_data_block_data = malloc(BLOCK_SIZE);
				memset(new_data_block_data, 0, BLOCK_SIZE);
				// Add dirent to the new data block
				struct dirent *new_entries = (struct dirent *)new_data_block_data;
				new_entries[0].valid = 1;
				new_entries[0].ino = f_ino;
				strncpy(new_entries[0].name, fname, name_len);
				new_entries[0].name[name_len] = '\0';
                new_entries[0].len = name_len;
				// Write the new data block to the disk
				if(bio_write(new_data_block, new_data_block_data) <= 0)
				{
					free(new_data_block_data);
					return -1;
				}

				free(new_data_block_data);
				return 0;
			}
			else
			{
                int indirect_block_index = dir_inode.indirect_ptr[i];
                memset(temp_block, 0, BLOCK_SIZE);
                if(bio_read(indirect_block_index, temp_block) <= 0)
                {
                    return -1;
                }

                int *entries = (int *)temp_block;
                for(int j=0; j<BLOCK_SIZE/sizeof(int); j++)
                {
                    // if entries[j] == 0, then allocate a block, write at 0th, and retun 0
                    if(entries[j] == 0)
                    {
                        // first, allocate a new block where we will store our dirent
                        int new_data_block = get_avail_blkno();
                        if(new_data_block == -1)
                            return -1;
                        
                        // update the new_indirect_block with the new_data_block
                        void *indirect_block_data = malloc(BLOCK_SIZE);
                        memset(indirect_block_data, 0, BLOCK_SIZE);
                        if(bio_read(indirect_block_index, indirect_block_data) <= 0)
                        {
                            free(indirect_block_data);
                            return -1;
                        }

                        int *indirect_entries = (int *)indirect_block_data;
                        indirect_entries[j] = new_data_block;

                        // write the indirect pointer data block back to disk
                        if(bio_write(indirect_block_index, indirect_block_data) <= 0)
                        {
                            free(indirect_block_data);
                            return -1;
                        }
                        free(indirect_block_data);

                        // Now we need to update the inode info on the disk
                        time_t current_time = time(NULL);
                        dir_inode.vstat.st_atime = current_time;
                        dir_inode.vstat.st_mtime = current_time;
                        dir_inode.size += sizeof(struct dirent);
                        if(writei(dir_inode.ino, &dir_inode) != 0)
                            return -1;
                        
                        // Fourth, initialize the new data block
                        void *new_data_block_data = malloc(BLOCK_SIZE);
                        memset(new_data_block_data, 0, BLOCK_SIZE);

                        // Add dirent to the new data block
                        struct dirent *new_entries = (struct dirent *)new_data_block_data;
                        new_entries[0].valid = 1;
                        new_entries[0].ino = f_ino;
                        strncpy(new_entries[0].name, fname, name_len);
                        new_entries[0].name[name_len] = '\0';
                        new_entries[0].len = name_len;

                        // Write the new data block to the disk
                        if(bio_write(new_data_block, new_data_block_data) <= 0)
                        {
                            free(new_data_block_data);
                            return -1;
                        }

                        free(new_data_block_data);
                        return 0;
                    }
                    else
                    {
                        // iterate over the block to see if there is any empty space to write. If could write return 0
                        
                        int new_data_block = entries[j];
                        
                        // get the new data block
                        void *new_data_block_data = malloc(BLOCK_SIZE);
                        memset(new_data_block_data, 0, BLOCK_SIZE);
                        bio_read(new_data_block, new_data_block_data);

                        // Add dirent to the new data block
                        struct dirent *new_entries = (struct dirent *)new_data_block_data;
                        
                        for(int k=0; k<BLOCK_SIZE/sizeof(struct dirent); k++)
                        {
                            if(new_entries[k].valid == 0)
                            {
                                new_entries[k].valid = 1;
                                new_entries[k].ino = f_ino;
                                strncpy(new_entries[k].name, fname, name_len);
                                new_entries[k].name[name_len] = '\0';
                                new_entries[k].len = name_len;

                                // we need to update the inode info on the disk
                                time_t current_time = time(NULL);
                                dir_inode.vstat.st_atime = current_time;
                                dir_inode.vstat.st_mtime = current_time;
                                dir_inode.size += sizeof(struct dirent);
                                if(writei(dir_inode.ino, &dir_inode) != 0)
                                    return -1;

                                // Write the data block to the disk
                                if(bio_write(new_data_block, new_data_block_data) <= 0)
                                {
                                    free(new_data_block_data);
                                    return -1;
                                }

                                free(new_data_block_data);
                                return 0;
                            }
                        }
                    }
                }
			}
		}
	}
    if(debugging == 1)
    {
        puts("exited dir_add\n");
        fflush(stdout);
    }
	return -1;
}


// Optional - Implemented, Also handeled indirect pointers
int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

    if(debugging == 1)
    {
        puts("\nentered dir_remove");
        fflush(stdout);
    }

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	// Step 2: Check if fname exist
	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

    // handling direct pointers
    for(int i=0; i<16; i++)
    {
        if(dir_inode.direct_ptr[i] != -1)
        {
            int index = dir_inode.direct_ptr[i];
            memset(temp_block, 0, BLOCK_SIZE);
            bio_read(index, temp_block);
            struct dirent *entries = (struct dirent *)temp_block;
            for(int j=0; j<BLOCK_SIZE/sizeof(struct dirent); j++)
            {
                if(entries[j].valid != 0)
                {
                    if(strncmp(entries[j].name, fname, name_len) == 0)
                    {
                        // Found the desired entry
                        entries[j].valid = 0;
                        entries[j].len = 0;
                        memset(entries[j].name, '\0', sizeof(entries[j].name));

                        // unset inode for this dirent
                        unset_bitmap(inode_bitmap, entries[j].ino);

                        // write the block back to disk
                        bio_write(index, temp_block);

                        return 0;
                    }
                }
            }
        }
    }

    // handling indirect pointers
    for(int i=0; i<8; i++)
    {
        if(dir_inode.indirect_ptr[i] != -1)
        {
            int index = dir_inode.indirect_ptr[i];
            memset(temp_block, 0, BLOCK_SIZE);
            bio_read(index, temp_block);

            int *entries = (int *)temp_block;
            for(int j=0; j<BLOCK_SIZE/sizeof(int); j++)
            {
                if(entries[j] != 0)
                {
                    void *block = malloc(BLOCK_SIZE);
                    memset(block, 0, BLOCK_SIZE);
                    bio_read(entries[j], block);

                    struct dirent *entries1 = (struct dirent *)block;
                    for(int k=0; k<BLOCK_SIZE/sizeof(struct dirent); k++)
                    {
                        if(entries1[k].valid != 0)
                        {
                            if(strncmp(entries1[k].name, fname, name_len) == 0)
                            {
                                // Found the desired entry
                                entries1[k].valid = 0;
                                entries1[k].len = 0;
                                memset(entries1[k].name, '\0', sizeof(entries1[k].name));

                                // unset inode for this dirent
                                unset_bitmap(inode_bitmap, entries1[k].ino);

                                // write the block back to disk
                                bio_write(entries[j], block);

                                return 0;
                            }
                        }
                    }
                    free(block);
                }
            }
        }
    }

    if(debugging == 1)
    {
        puts("exited dir_remove\n");
        fflush(stdout);
    }

	return 0;
}


/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {

    if(debugging == 1)
    {
        puts("\nentered get_node_by_path");
        fflush(stdout);
    }

    // Step 1: Start with the root inode
    struct inode current_inode;
    if (readi(ino, &current_inode) != 0) {
        fprintf(stderr, "Error reading root inode\n");
        return -1;
    }

    // Step 2: Tokenize the path
    char *token = strtok((char *)path, "/");
    while (token != NULL) {
        // Step 3: Look up the current directory for the token
        struct dirent dir_entry;

        if (dir_find(current_inode.ino, token, strlen(token), &dir_entry) != 0) {
            printf("Error finding directory entry for %s\n", token);
            fflush(stdout);
            return -1;
        }

        // Step 4: Read the inode of the found entry
        if (readi(dir_entry.ino, &current_inode) != 0) {
            printf("Error reading inode for %s\n", token);
            fflush(stdout);
            return -1;
        }

        // Step 5: Move to the next token
        token = strtok(NULL, "/");
    }

    // Step 6: Copy the final inode to the output parameter
    memcpy(inode, &current_inode, sizeof(struct inode));

    if(debugging == 1)
    {
        puts("exited get_node_by_path\n");
        fflush(stdout);
    }

    return 0;
}


/* 
 * Make file system
 */
int rufs_mkfs() {

    if(debugging == 1)
    {
        puts("\nentered rufs_mkfs");
        fflush(stdout);
    }

    temp_block = malloc(BLOCK_SIZE);

    // Call dev_init() to initialize (Create) Diskfile
    dev_init(diskfile_path);

    // write superblock information
    sb = malloc(sizeof(struct superblock));
    sb->magic_num = MAGIC_NUM;
    sb->max_inum = MAX_INUM;
    sb->max_dnum = MAX_DNUM;
    sb->i_bitmap_blk = 1;
    sb->d_bitmap_blk = 2;
    sb->i_start_blk = 3;
    sb->d_start_blk = ((MAX_INUM*sizeof(struct inode))/BLOCK_SIZE)+3;
    bio_write(0, sb);

    // initialize inode bitmap
    inode_bitmap = malloc(MAX_INUM / 8);
    memset(inode_bitmap, 0, MAX_INUM / 8);

    // initialize data block bitmap
    datablock_bitmap = malloc(MAX_DNUM / 8);
    memset(datablock_bitmap, 0, MAX_DNUM / 8);

    // update bitmap information for root directory
    set_bitmap(inode_bitmap, 0);
    bio_write(sb->i_bitmap_blk, inode_bitmap);
    bio_write(sb->d_bitmap_blk, datablock_bitmap);

    // update inode for the root directory
    struct inode root_inode;
    root_inode.ino = 0;        // Inode number for the root directory
    root_inode.valid = 1;      // Set as a valid inode
    root_inode.size = 0;       // Size of the root directory
    root_inode.type = __S_IFDIR; // Set as a directory
    root_inode.link = 2;       // Two links: one for itself and one for its parent

    // Initialize direct pointers
    for (int i = 0; i < 16; ++i)
        root_inode.direct_ptr[i] = -1;

    // Initialize indirect pointers
    for (int i = 0; i < 8; ++i)
        root_inode.indirect_ptr[i] = -1;

    // Initialize the inode stat structure
    root_inode.vstat.st_dev = 0;
    root_inode.vstat.st_ino = root_inode.ino;
    root_inode.vstat.st_mode = __S_IFDIR | 0755;  // Directory with permissions 0755
    root_inode.vstat.st_nlink = root_inode.link;
    root_inode.vstat.st_uid = getuid();
    root_inode.vstat.st_gid = getgid();
    root_inode.vstat.st_rdev = 0;
    root_inode.vstat.st_size = root_inode.size;
    root_inode.vstat.st_blksize = BLOCK_SIZE;
    root_inode.vstat.st_blocks = 0;

    time_t current_time = time(NULL);
    root_inode.vstat.st_atime = current_time;
    root_inode.vstat.st_mtime = current_time;
    
    // Write the initialized root inode to the disk
    // if (writei(root_inode.ino, &root_inode) != 0)
    //     return -1;

	memset(temp_block, 0, BLOCK_SIZE);
    memcpy(temp_block, &root_inode, sizeof(struct inode));
    bio_write(sb->i_start_blk, temp_block);

    if(debugging == 1)
    {
        puts("exited rufs_mkfs\n");
        fflush(stdout);
    }

    return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {

    if(debugging == 1)
    {
        puts("\nentered rufs_init");
        fflush(stdout);
    }

    // Step 1a: If disk file is not found, call mkfs
    // Step 1b: If disk file is found, just initialize in-memory data structures
    // and read superblock from disk
    if (dev_open(diskfile_path) == -1)
    {
        rufs_mkfs();
    }
    else
    {
        temp_block = malloc(BLOCK_SIZE);
        inode_bitmap = malloc(BLOCK_SIZE);
        datablock_bitmap = malloc(BLOCK_SIZE);
        sb = malloc(sizeof(struct superblock));

        memset(temp_block, 0, BLOCK_SIZE);
        if (bio_read(0, temp_block) < 0)
        {
            printf("Error reading superblock\n");
            fflush(stdout);
            exit(EXIT_FAILURE);
        }
        memcpy(sb, temp_block, sizeof(struct superblock));
        memset(temp_block, 0, BLOCK_SIZE);

        if (bio_read(sb->i_bitmap_blk, inode_bitmap) < 0)
        {
            printf("Error reading inode bitmap\n");
            fflush(stdout);
            exit(EXIT_FAILURE);
        }

        if (bio_read(sb->d_bitmap_blk, datablock_bitmap) < 0)
        {
            printf("Error reading data block bitmap\n");
            fflush(stdout);
            exit(EXIT_FAILURE);
        }
    }

    if(debugging == 1)
    {
        puts("exited rufs_init\n");
        fflush(stdout);
    }

    return NULL;
}


static void rufs_destroy(void *userdata) {

    if(debugging == 1)
    {
        puts("\nentered rufs_destroy");
        fflush(stdout);
    }

    // write superblock, and bitmaps to disk
    bio_write(0, sb);
    bio_write(sb->i_bitmap_blk, inode_bitmap);
    bio_write(sb->d_bitmap_blk, datablock_bitmap);

    // Step 1: De-allocate in-memory data structures
    free(inode_bitmap);
    free(sb);
    free(temp_block);

    // Step 2: Close diskfile
    dev_close();

	int num_blks_used = 0;
	for (int i = 0; i < MAX_DNUM; i++)
	{
		if (get_bitmap(datablock_bitmap, i) == 1)
			num_blks_used++;
	}
	printf("Number of block used: %d\n", num_blks_used);

    free(datablock_bitmap);
	
    dev_close(diskfile_path);

    if(debugging == 1)
    {
        puts("exited rufs_destroy\n");
        fflush(stdout);
    }
}


static int rufs_getattr(const char *path, struct stat *stbuf) {

    if(debugging == 1)
    {
        puts("\nentered rufs_getattr");
        fflush(stdout);
    }

	// Step 1: call get_node_by_path() to get inode from path
    struct inode target_inode;
    if (get_node_by_path(path, 0, &target_inode) != 0) {
        return -ENOENT;
    }

	// Step 2: fill attribute of file into stbuf from inode

        // professor given
		// stbuf->st_mode   = __S_IFDIR | 0755;
		// stbuf->st_nlink  = 2;
		// time(&stbuf->st_mtime);

    stbuf->st_mode = target_inode.vstat.st_mode;
    stbuf->st_nlink = target_inode.link;
    stbuf->st_uid = target_inode.vstat.st_uid;
    stbuf->st_gid = target_inode.vstat.st_gid;
    stbuf->st_size = target_inode.size;
    stbuf->st_atime = target_inode.vstat.st_atime;
    stbuf->st_mtime = target_inode.vstat.st_mtime;
    //stbuf->st_ctime = target_inode.vstat.st_ctime;

    if (S_ISDIR(stbuf->st_mode)) {
        // If it's a directory, set appropriate mode and link count
        stbuf->st_mode |= __S_IFDIR;
        stbuf->st_nlink = 2;  // Default for directories
    } else {
        // If it's a regular file, set appropriate mode
        stbuf->st_mode |= __S_IFREG;
    }

    if(debugging == 1)
    {
        puts("exited rufs_getattr\n");
        fflush(stdout);
    }

	return 0;
}


static int rufs_opendir(const char *path, struct fuse_file_info *fi) {

    if(debugging == 1)
    {
        puts("\nentered rufs_opendir");
        fflush(stdout);
    }

	// Step 1: Call get_node_by_path() to get inode from path
    struct inode file_inode;
    if (get_node_by_path(path, 0, &file_inode) != 0) {
        fprintf(stderr, "Error getting inode for %s\n", path);
        return -ENOENT; // Return appropriate error code for "No such file or directory"
    }

	// Step 2: If not find, return -1
    if (!file_inode.valid) {
        fprintf(stderr, "Inode for %s is not valid\n", path);
        return -ENOENT; // Return appropriate error code for "No such file or directory"
    }

    if(debugging == 1)
    {
        puts("exited rufs_opendir\n");
        fflush(stdout);
    }

    return 0;
}


static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

    if(debugging == 1)
    {
        puts("\nentered rufs_readdir");
        fflush(stdout);
    }

	// Step 1: Call get_node_by_path() to get inode from path
    struct inode target_inode;
    if (get_node_by_path(path, 0, &target_inode) != 0) {
        fprintf(stderr, "Error getting inode for %s\n", path);
        return -ENOENT; // Return appropriate error code for "No such file or directory"
    }

	// Step 2: Read directory entries from its data blocks, and copy them to filler
    // handling direct pointers
    for(int i=0; i<16; i++)
    {
        if(target_inode.direct_ptr[i] != -1)
        {
            int index = target_inode.direct_ptr[i];
            memset(temp_block, 0, BLOCK_SIZE);
            bio_read(index, temp_block);
            struct dirent *entries = (struct dirent *)temp_block;
            for(int j=0; j<BLOCK_SIZE/sizeof(struct dirent); j++)
            {
                if(entries[j].valid != 0)
                {
                    if (filler(buffer, entries[j].name, NULL, offset) != 0)
                    {
                        fprintf(stderr, "Error adding directory entry to buffer\n");
                        return -ENOMEM;
                    }
                }
            }
        }
    }

    // handling indirect pointers
    for(int i=0; i<8; i++)
    {
        if(target_inode.indirect_ptr[i] != -1)
        {
            int index = target_inode.indirect_ptr[i];
            memset(temp_block, 0, BLOCK_SIZE);
            bio_read(index, temp_block);

            int *entries = (int *)temp_block;
            for(int j=0; j<BLOCK_SIZE/sizeof(int); j++)
            {
                if(entries[j] != 0)
                {
                    void *block = malloc(BLOCK_SIZE);
                    memset(block, 0, BLOCK_SIZE);
                    bio_read(entries[j], block);

                    struct dirent *entries1 = (struct dirent *)block;
                    for(int k=0; k<BLOCK_SIZE/sizeof(struct dirent); k++)
                    {
                        if(entries1[k].valid != 0)
                        {
                            if (filler(buffer, entries1[k].name, NULL, offset) != 0)
                            {
                                fprintf(stderr, "Error adding directory entry to buffer\n");
                                return -ENOMEM;
                            }
                        }
                    }
                    free(block);
                }
            }
        }
    }

    if(debugging == 1)
    {
        puts("exited rufs_readdir\n");
    }

	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	// Step 2: Call get_node_by_path() to get inode of parent directory
	// Step 3: Call get_avail_ino() to get an available inode number
	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	// Step 5: Update inode for target directory
	// Step 6: Call writei() to write inode to disk

    if(debugging == 1)
    {
        puts("\nentered rufs_mkdir");
        fflush(stdout);
    }

    // Step 1: Use dirname() and basename() to separate parent directory path and target file name
    char *parent_dir_path = strdup(path);
    char *file_name = strdup(path);
    char *parent_dir = dirname(parent_dir_path);
    char *target_file = basename(file_name);

    // Step 2: Call get_node_by_path() to get inode of parent directory
    struct inode parent_inode;
    if (get_node_by_path(parent_dir, 0, &parent_inode) != 0) {
        fprintf(stderr, "Error getting inode for parent directory %s\n", parent_dir);
        free(parent_dir_path);
        free(file_name);
        return -ENOENT; // Return appropriate error code for "No such file or directory"
    }

    // Step 3: Call get_avail_ino() to get an available inode number
    int new_inode_number = get_avail_ino();
    if (new_inode_number == -1) {
        fprintf(stderr, "Error getting an available inode number\n");
        free(parent_dir_path);
        free(file_name);
        return -ENOMEM; // Return appropriate error code for "Out of memory"
    }

    // Step 4: Call dir_add() to add directory entry of target file to parent directory
    if (dir_add(parent_inode, new_inode_number, target_file, strlen(target_file)) == -1) {
        fprintf(stderr, "Error adding directory entry for %s in %s\n", target_file, parent_dir);
        free(parent_dir_path);
        free(file_name);
        return -EIO; // Return appropriate error code for "Input/output error"
    }

    // Step 5: Update inode for target file
    struct inode target_inode;
    target_inode.ino = new_inode_number;
    target_inode.valid = 1;
    target_inode.size = 0; // Set the initial size to 0 for a new file
    target_inode.type = __S_IFDIR | (mode & 0755); // Regular directory with permissions
    target_inode.link = 2; // One link to itself

    // Initialize direct pointers
    for (int i = 0; i < 16; ++i)
        target_inode.direct_ptr[i] = -1;

    // Initialize indirect pointers
    for (int i = 0; i < 8; ++i)
        target_inode.indirect_ptr[i] = -1;

    // Initialize the inode stat structure
    target_inode.vstat.st_dev = 0;
    target_inode.vstat.st_ino = target_inode.ino;
    target_inode.vstat.st_mode = target_inode.type;
    target_inode.vstat.st_nlink = target_inode.link;
    target_inode.vstat.st_uid = getuid();
    target_inode.vstat.st_gid = getgid();
    target_inode.vstat.st_rdev = 0;
    target_inode.vstat.st_size = target_inode.size;
    target_inode.vstat.st_blksize = BLOCK_SIZE;
    target_inode.vstat.st_blocks = 0;

    time_t current_time = time(NULL);
    target_inode.vstat.st_atime = current_time;
    target_inode.vstat.st_mtime = current_time;

    // Step 6: Call writei() to write inode to disk
    if (writei(target_inode.ino, &target_inode) == -1) {
        fprintf(stderr, "Error writing inode for %s\n", target_file);
        free(parent_dir_path);
        free(file_name);
        return -EIO; // Return appropriate error code for "Input/output error"
    }

    free(parent_dir_path);
    free(file_name);

    if(debugging == 1)
    {
        puts("exited rufs_mkdir\n");
        fflush(stdout);
    }

	return 0;
}


// Optional - Implemented, Also handeled indirect pointers
static int rufs_rmdir(const char *path) {

    if(debugging == 1)
    {
        puts("\nentered rufs_rmdir");
        fflush(stdout);
    }

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
    char *parent_dir_path = strdup(path);
    char *file_name = strdup(path);
    char *parent_dir = dirname(parent_dir_path);
    char *target_file = basename(file_name);

	// Step 2: Call get_node_by_path() to get inode of target directory
    struct inode target_inode;
    if (get_node_by_path(path, 0, &target_inode) != 0) {
        free(parent_dir_path);
        free(file_name);
        return -ENOENT;
    }

	// Step 3: Clear data block bitmap of target directory
    // handling direct pointers
    if(target_inode.size > 0)
    {
        return -ENOTEMPTY;
    }

	// Step 4: Clear inode bitmap and its data block
    unset_bitmap(inode_bitmap, target_inode.ino);

	// Step 5: Call get_node_by_path() to get inode of parent directory
    struct inode parent_inode;
    if (get_node_by_path(parent_dir, 0, &parent_inode) != 0) {
        free(parent_dir_path);
        free(file_name);
        return -ENOENT;
    }

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
    dir_remove(parent_inode, target_file, strlen(target_file));
    time_t current_time = time(NULL);
    parent_inode.vstat.st_atime = current_time;
    parent_inode.vstat.st_mtime = current_time;
    parent_inode.size -= sizeof(struct dirent);
    writei(parent_inode.ino, &parent_inode);

    if(debugging == 1)
    {
        puts("exited rufs_rmdir\n");
        fflush(stdout);
    }

	return 0;
}


static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

    if(debugging == 1)
    {
        puts("\nentered rufs_create");
        fflush(stdout);
    }

    // Step 1: Use dirname() and basename() to separate parent directory path and target file name
    char *parent_dir_path = strdup(path);
    char *file_name = strdup(path);
    char *parent_dir = dirname(parent_dir_path);
    char *target_file = basename(file_name);

    // Step 2: Call get_node_by_path() to get inode of parent directory
    struct inode parent_inode;
    if (get_node_by_path(parent_dir, 0, &parent_inode) != 0) {
        fprintf(stderr, "Error getting inode for parent directory %s\n", parent_dir);
        free(parent_dir_path);
        free(file_name);
        return -ENOENT; // Return appropriate error code for "No such file or directory"
    }

    // Step 3: Call get_avail_ino() to get an available inode number
    int new_inode_number = get_avail_ino();
    if (new_inode_number == -1) {
        fprintf(stderr, "Error getting an available inode number\n");
        free(parent_dir_path);
        free(file_name);
        return -ENOMEM; // Return appropriate error code for "Out of memory"
    }

    // Step 4: Call dir_add() to add directory entry of target file to parent directory
    if (dir_add(parent_inode, new_inode_number, target_file, strlen(target_file)) == -1) {
        fprintf(stderr, "Error adding directory entry for %s in %s\n", target_file, parent_dir);
        free(parent_dir_path);
        free(file_name);
        return -EIO; // Return appropriate error code for "Input/output error"
    }

    // Step 5: Update inode for target file
    struct inode target_inode;
    target_inode.ino = new_inode_number;
    target_inode.valid = 1;
    target_inode.size = 0; // Set the initial size to 0 for a new file
    target_inode.type = __S_IFREG | (mode & 0777); // Regular file with permissions
    target_inode.link = 1; // One link to itself

    // Initialize direct pointers
    for (int i = 0; i < 16; ++i)
        target_inode.direct_ptr[i] = -1;

    // Initialize indirect pointers
    for (int i = 0; i < 8; ++i)
        target_inode.indirect_ptr[i] = -1;

    // Initialize the inode stat structure
    target_inode.vstat.st_dev = 0;
    target_inode.vstat.st_ino = target_inode.ino;
    target_inode.vstat.st_mode = target_inode.type;
    target_inode.vstat.st_nlink = target_inode.link;
    target_inode.vstat.st_uid = getuid();
    target_inode.vstat.st_gid = getgid();
    target_inode.vstat.st_rdev = 0;
    target_inode.vstat.st_size = target_inode.size;
    target_inode.vstat.st_blksize = BLOCK_SIZE;
    target_inode.vstat.st_blocks = 0;

    time_t current_time = time(NULL);
    target_inode.vstat.st_atime = current_time;
    target_inode.vstat.st_mtime = current_time;

    // Step 6: Call writei() to write inode to disk
    if (writei(target_inode.ino, &target_inode) == -1) {
        fprintf(stderr, "Error writing inode for %s\n", target_file);
        free(parent_dir_path);
        free(file_name);
        return -EIO; // Return appropriate error code for "Input/output error"
    }

    free(parent_dir_path);
    free(file_name);

    if(debugging == 1)
    {
        puts("exited rufs_create\n");
        fflush(stdout);
    }

    return 0;
}


static int rufs_open(const char *path, struct fuse_file_info *fi) {
    if (debugging == 1) {
        puts("\nentered rufs_open");
        fflush(stdout);
    }

    // Step 1: Call get_node_by_path() to get inode from path
    struct inode file_inode;
    if (get_node_by_path(path, 0, &file_inode) != 0) {
        fprintf(stderr, "Error getting inode for %s\n", path);
        return -ENOENT; // Return appropriate error code for "No such file or directory"
    }

    // Step 2: If not found, return -1
    if (!file_inode.valid) {
        fprintf(stderr, "Inode for %s is not valid\n", path);
        return -ENOENT; // Return appropriate error code for "No such file or directory"
    }

    if (debugging == 1) {
        puts("exited rufs_open\n");
        fflush(stdout);
    }

    return 0;
}


static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    if (debugging == 1) {
        puts("\nentered rufs_read");
        fflush(stdout);
    }

    // Step 1: You could call get_node_by_path() to get inode from path
    // Step 2: Based on size and offset, read its data blocks from disk
    // Step 3: copy the correct amount of data from offset to buffer

    struct inode target_inode;
    if (get_node_by_path(path, 0, &target_inode) != 0) {
        puts("Error getting inode for the target inode");
        return -ENOENT; // Return appropriate error code for "No such file or directory"
    }

    int temp_size = 0;
    int read_loc_in_blk = (offset % BLOCK_SIZE);
    int start_blk = offset / BLOCK_SIZE;

    while (temp_size < size) {
        // handling direct pointers
        if (start_blk < 16) {
            memset(temp_block, 0, BLOCK_SIZE);
            // get the block locally
            bio_read(target_inode.direct_ptr[start_blk], temp_block);

            // read from block
            int limit = (size - temp_size) < (BLOCK_SIZE - read_loc_in_blk) ? (size - temp_size) : (BLOCK_SIZE - read_loc_in_blk);
            memcpy(buffer + temp_size, (char *)temp_block + read_loc_in_blk, limit);

            temp_size += limit;
            start_blk++;
            read_loc_in_blk = 0;
        } else {
            // handling indirect pointers
            // removing direct data blocks
            start_blk -= 16;
            start_blk /= (BLOCK_SIZE / sizeof(int));
            int start_blk_db_offset = start_blk % (BLOCK_SIZE / sizeof(int));

            // knowing the data block index and reading it
            int ip_index = target_inode.indirect_ptr[start_blk];
            // if the indirect pointer is not allocated, allocate it
            if (ip_index == -1) {
                ip_index = get_avail_blkno();
                target_inode.indirect_ptr[start_blk] = ip_index;
                // write indirect pointer data block back to disk as it was modified
                bio_write(ip_index, temp_block);
            }

            memset(temp_block, 0, BLOCK_SIZE);
            bio_read(ip_index, temp_block);
            int *entries = (int *)temp_block;
            int db_to_read = entries[start_blk_db_offset];

            if (db_to_read == 0) {
                db_to_read = get_avail_blkno();
                entries[start_blk_db_offset] = db_to_read;
                memset(temp_block, 0, BLOCK_SIZE);
                bio_write(db_to_read, temp_block);
            }

            memset(temp_block, 0, BLOCK_SIZE);
            bio_read(db_to_read, temp_block);

            // read from block
            int limit = (size - temp_size) < (BLOCK_SIZE - read_loc_in_blk) ? (size - temp_size) : (BLOCK_SIZE - read_loc_in_blk);
            memcpy(buffer + temp_size, (char *)temp_block + read_loc_in_blk, limit);

            temp_size += limit;
            start_blk++;
            read_loc_in_blk = 0;
        }
    }

    // Step 4: Update the inode info and write it to disk
    time_t current_time = time(NULL);
    target_inode.vstat.st_atime = current_time;
    target_inode.vstat.st_mtime = current_time;
    if (writei(target_inode.ino, &target_inode) != 0) {
        return -1;
    }

    if (debugging == 1) {
        puts("exited rufs_read\n");
        fflush(stdout);
    }

    // Note: this function should return the amount of bytes you copied to buffer
    return size;
}



static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    if (debugging == 1) {
        puts("\nentered rufs_write");
        fflush(stdout);
    }

    // Step 1: You could call get_node_by_path() to get inode from path
    struct inode target_inode;
    if (get_node_by_path(path, 0, &target_inode) != 0) {
        puts("Error getting inode for the target inode");
        return -ENOENT; // Return appropriate error code for "No such file or directory"
    }

    // Step 2: Based on size and offset, read its data blocks from disk
    // Step 3: Write the correct amount of data from offset to disk
    int temp_size = 0;
    int write_loc_in_blk = offset % BLOCK_SIZE;
    int start_blk = offset / BLOCK_SIZE;

    while (temp_size < size) {
        // handling direct pointers
        if (start_blk < 16) {
            memset(temp_block, 0, BLOCK_SIZE);
            // get the block locally
            if (target_inode.direct_ptr[start_blk] == -1) {
                target_inode.direct_ptr[start_blk] = get_avail_blkno();
            }
            bio_read(target_inode.direct_ptr[start_blk], temp_block);

            // write in block
            int limit = (size - temp_size) < (BLOCK_SIZE - write_loc_in_blk) ? (size - temp_size) : (BLOCK_SIZE - write_loc_in_blk);
            memcpy((char *)temp_block + write_loc_in_blk, buffer + temp_size, limit);

            // write data block back to disk
            bio_write(target_inode.direct_ptr[start_blk], temp_block);

            temp_size += limit;
            start_blk++;
            write_loc_in_blk = 0;
        } else {
            // handling indirect pointers
            // removing direct data blocks
            start_blk -= 16;
            start_blk /= (BLOCK_SIZE / sizeof(int));
            int start_blk_db_offset = start_blk % (BLOCK_SIZE / sizeof(int));

            // knowing the data block index and reading it
            int ip_index = target_inode.indirect_ptr[start_blk];

            // if the indirect pointer is not allocated, allocate it
            if (ip_index == -1) {
                ip_index = get_avail_blkno();
                target_inode.indirect_ptr[start_blk] = ip_index;
                // write indirect pointer data block back to disk as it was modified
                memset(temp_block, 0, BLOCK_SIZE);
                bio_write(ip_index, temp_block);
            }

            memset(temp_block, 0, BLOCK_SIZE);
            bio_read(ip_index, temp_block);
            int *entries = (int *)temp_block;
            int db_to_read = entries[start_blk_db_offset];

            if (db_to_read == 0) {
                db_to_read = get_avail_blkno();
                entries[start_blk_db_offset] = db_to_read;
                memset(temp_block, 0, BLOCK_SIZE);
                bio_write(db_to_read, temp_block);
            }

            memset(temp_block, 0, BLOCK_SIZE);
            bio_read(db_to_read, temp_block);

            // write in block
            int limit = (size - temp_size) < (BLOCK_SIZE - write_loc_in_blk) ? (size - temp_size) : (BLOCK_SIZE - write_loc_in_blk);
            memcpy((char *)temp_block + write_loc_in_blk, buffer + temp_size, limit);

            // write data block back to disk
            bio_write(db_to_read, temp_block);

            temp_size += limit;
            start_blk++;
            write_loc_in_blk = 0;
        }
    }

    // Step 4: Update the inode info and write it to disk
    time_t current_time = time(NULL);
    target_inode.vstat.st_atime = current_time;
    target_inode.vstat.st_mtime = current_time;
    target_inode.size += ((offset + size) - target_inode.size);

    if (writei(target_inode.ino, &target_inode) != 0) {
        return -1;
    }

    if(debugging == 1)
    {
        puts("exited rufs_write\n");
        fflush(stdout);
    }

    // Note: this function should return the amount of bytes you write to disk
    return size;
}


// Optional
static int rufs_unlink(const char *path) {

    if(debugging == 1)
    {
        puts("\nentered rufs_unlink");
        fflush(stdout);
    }

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
    char *parent_dir_path = strdup(path);
    char *file_name = strdup(path);
    char *parent_dir = dirname(parent_dir_path);
    char *target_file = basename(file_name);

	// Step 2: Call get_node_by_path() to get inode of target file
    struct inode target_inode;
    if (get_node_by_path(path, 0, &target_inode) != 0) {
        free(parent_dir_path);
        free(file_name);
        return -ENOENT;
    }

	// Step 3: Clear data block bitmap of target file
    // handling direct pointers
    for(int i=0; i<16; i++)
    {
        if(target_inode.direct_ptr[i] != -1)
        {
            int index = target_inode.direct_ptr[i];
            unset_bitmap(datablock_bitmap, index);
        }
    }

    // handling indirect pointers
    for(int i=0; i<8; i++)
    {
        if(target_inode.indirect_ptr[i] != -1)
        {
            int index = target_inode.indirect_ptr[i];
            memset(temp_block, 0, BLOCK_SIZE);
            bio_read(index, temp_block);
            int *entries = (int *)temp_block;
            for(int j=0; j<BLOCK_SIZE/sizeof(int); j++)
            {
                if(entries[j] != 0)
                {
                    unset_bitmap(datablock_bitmap, entries[j]);
                }
            }
            unset_bitmap(datablock_bitmap, index);
        }
    }

	// Step 4: Clear inode bitmap and its data block
    unset_bitmap(inode_bitmap, target_inode.ino);

	// Step 5: Call get_node_by_path() to get inode of parent directory
    struct inode parent_inode;
    if (get_node_by_path(parent_dir, 0, &parent_inode) != 0) {
        free(parent_dir_path);
        free(file_name);
        return -ENOENT;
    }

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory
    dir_remove(parent_inode, target_file, strlen(target_file));
    time_t current_time = time(NULL);
    parent_inode.vstat.st_atime = current_time;
    parent_inode.vstat.st_mtime = current_time;
    parent_inode.size -= sizeof(struct dirent);
    writei(parent_inode.ino, &parent_inode);

    if(debugging == 1)
    {
        puts("exited rufs_rmdir\n");
        fflush(stdout);
    }

	return 0;
}


static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}


static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}

