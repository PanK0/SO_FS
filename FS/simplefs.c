#include "simplefs.h"

// initializes a file system on an already made disk
// returns a handle to the top level directory stored in the first block
DirectoryHandle* SimpleFS_init(SimpleFS* fs, DiskDriver* disk) {

	fs->disk = disk;
	
	// Creating the Directory Handle and filling it
	DirectoryHandle* handle  = (DirectoryHandle*) malloc(sizeof(DirectoryHandle));
	FirstDirectoryBlock* firstdir = (FirstDirectoryBlock*) malloc(sizeof(FirstDirectoryBlock));
	
	// If operating on a new disk return NULL: we need to format it.
	int snorlax = DiskDriver_readBlock(disk, firstdir, 0);
	if (snorlax) return NULL;
	
	// Filling the handle
	handle->sfs = fs;
	handle->dcb = firstdir;
	handle->directory = NULL;
	handle->current_block = (&firstdir->header);
	handle->pos_in_dir = 0;
	handle->pos_in_block = 0;
	
	return handle;	
}

// creates the inital structures, the top level directory
// has name "/" and its control block is in the first position
// it also clears the bitmap of occupied blocks on the disk
// the current_directory_block is cached in the SimpleFS struct
// and set to the top level directory
void SimpleFS_format(SimpleFS* fs) {
	
	// First of all, free the disk
	int num_blocks = fs->disk->header->num_blocks;
	for (int i = 0; i < num_blocks; ++i) {
		DiskDriver_freeBlock(fs->disk, i);
	}
	
	// Once the disk is free, creating the Directory Header
	BlockHeader header;
	header.previous_block = TBA;
	header.next_block = TBA;
	header.block_in_file = 0;
	header.block_in_disk = 0;

	// Creating the FCB
	FileControlBlock fcb;
	fcb.directory_block = TBA;
	fcb.block_in_disk = 0;
	strcpy(fcb.name, "/");
	fcb.size_in_bytes = sizeof(FirstDirectoryBlock);
	fcb.size_in_blocks = 1;
	fcb.is_dir = DIR;
		
	// Creating the First Directory Block
	FirstDirectoryBlock firstdir;
	firstdir.header = header;
	firstdir.fcb = fcb;
	firstdir.num_entries = 0;
	int size = (BLOCK_SIZE - sizeof(BlockHeader) - sizeof(FileControlBlock) - sizeof(int)) / sizeof(int);
	for (int i = 0; i < size; ++i) {
		firstdir.file_blocks[i] = TBA;
	}
	
	DiskDriver_writeBlock(fs->disk, &firstdir, 0); 
	
}

// creates an empty file in the directory d
// returns null on error (file existing, no free blocks)
// an empty file consists only of a block of type FirstBlock
FileHandle* SimpleFS_createFile(DirectoryHandle* d, const char* filename) {
	
	// Checking for DirectoryHandle and Disk existance
	if (d == NULL) return NULL;
	
	DiskDriver* disk = d->sfs->disk;
	if (disk == NULL) return NULL;
	
	// Checking for remaining free blocks
	// Might need 2 free block to store the file block and an additional directory block
	if (disk->header->free_blocks <= 1) return NULL;
	
	// Creating useful things
	FileHandle* handle = (FileHandle*) malloc(sizeof(FileHandle));
	FirstFileBlock* file = (FirstFileBlock*) malloc(sizeof(FirstFileBlock));
	void* dirblock = (void*) malloc(BLOCK_SIZE);
	
	int fbsize = (BLOCK_SIZE
		   -sizeof(BlockHeader)
		   -sizeof(FileControlBlock)
			-sizeof(int))/sizeof(int);
	int bsize = (BLOCK_SIZE-sizeof(BlockHeader))/sizeof(int);
	int voyager = TBA;
	int snorlax = TBA;
	int filecount = 0;
	
	// This will assume one of these values:
	// LENNY_OK = No need to create another DirectoryBlock
	// LENNY_NEED = Need to create another DirectoryBlock
	int lenny = LENNY_OK;

	// Now checking for existing file
	// Scanning all blocks in file_blocks:
	// IF the block is occupied in the bitmap (snorlax = 0)
	// && IF the block is a file (IS_DIR = 0)
	// && IF the scanned file's name == filename
	// MEANS THAT already exists a file with name filename in that folder
	// SO it is not possible to create another one
	
	int i = 0;
	while (i < fbsize) {
		if (d->dcb->file_blocks[i] != TBA) {
			++filecount;
			snorlax = DiskDriver_readBlock(disk, file, d->dcb->file_blocks[i]);
			if (snorlax == 0 && file->fcb.is_dir == FIL) {
				if (strcmp(file->fcb.name, filename) == 0) {
					printf ("ALREADY EXISTS A FILE WITH THE SAME NAME!\n");
					return NULL;
				}
			}
		}
		++i;
	}
	if (filecount == i)	lenny = LENNY_NEED;
	dirblock = d->dcb;
	BlockHeader help_header = d->dcb->header;
	
	// If the directory is splitted in more blocks, control them
	int next_dirblock = d->dcb->header.next_block;
	while (next_dirblock != TBA) {
		lenny = LENNY_OK;
		//reads the dir block and scans it in search for an equal file
		snorlax = DiskDriver_readBlock(disk, dirblock, next_dirblock);
		if (snorlax) {
			printf ("ERROR : SNORLAX IS BLOCKING THE WAY\n");
			return NULL;
		}
		help_header = ((DirectoryBlock*)dirblock)->header;
		i = 0;
		filecount = 0;
		while (i < bsize) {
			if (((DirectoryBlock*)dirblock)->file_blocks[i] != TBA) {
				++filecount;
				snorlax = DiskDriver_readBlock(disk, file, ((DirectoryBlock*)dirblock)->file_blocks[i]);
				if (snorlax == 0 && file->fcb.is_dir == FIL) {
					if (strcmp(file->fcb.name, filename) == 0) {
						printf ("ALREADY EXISTS A FILE WITH THE SAME NAME!\n");
						return NULL;
					}
				}
			}
			++i;
		}
		next_dirblock = ((DirectoryBlock*)dirblock)->header.next_block;
	}
	if (filecount == i) lenny = LENNY_NEED;
	
	// Now all the directory blocks are checked if there is a file named filename
	// It's moment to create the new dirblock if needed
	// IF LENNY_NEED, help_header contains the header of the last dirblock visited
	
	if (lenny == LENNY_NEED) {
		
		// Updating dirblock's header and writing it into the disk
		voyager = DiskDriver_getFreeBlock(disk, 0);
		((DirectoryBlock*)dirblock)->header.next_block = voyager;
		snorlax = DiskDriver_writeBlock(disk, dirblock, ((DirectoryBlock*)dirblock)->header.block_in_disk);
		if (snorlax == ERROR_FILE_WRITING) {
			printf ("ERROR : SNORLAX IS BLOCKING THE WAY\n");
			return NULL;
		}
		
		// Creating the new dirblock in dirblock and writing it into the disk
		memset(dirblock, 0, sizeof(DirectoryBlock));
		((DirectoryBlock*)dirblock)->header.previous_block = help_header.block_in_disk;
		((DirectoryBlock*)dirblock)->header.next_block = TBA;
		((DirectoryBlock*)dirblock)->header.block_in_file = 0;
		((DirectoryBlock*)dirblock)->header.block_in_disk = voyager;
		
		for (int j = 0; j < bsize; ++j) {
			((DirectoryBlock*)dirblock)->file_blocks[j] = TBA;
		}
		
		snorlax = DiskDriver_writeBlock(disk, dirblock, voyager);
		if (snorlax == ERROR_FILE_WRITING) {
			printf ("ERROR : SNORLAX IS BLOCKING THE WAY\n");
			return NULL;
		}
		
		// Updating the fcb
		d->dcb->fcb.size_in_bytes += BLOCK_SIZE;
		d->dcb->fcb.size_in_blocks += 1;
		snorlax = DiskDriver_writeBlock(disk, d->dcb, d->dcb->fcb.block_in_disk);
		if (snorlax == ERROR_FILE_WRITING) {
			printf ("ERROR : SNORLAX IS BLOCKING THE WAY\n");
			return NULL;
		}
	}
	
/*	
	// Checking for existing file	
	// Scanning all blocks in file_blocks: 
	// IF the block is occupied in the bitmap (voyager = 0)
	// && IF the scanned file's name == filename
	// MEANS THAT already exists a file with name filename in that folder
	// SO it is not possible to create another one

	for (int i = 0; i < block->num_entries; ++i) {
		int voyager = DiskDriver_readBlock(disk, file, block->file_blocks[i]);
		if (voyager == 0) {		
			if (strcmp(file->fcb.name, filename) == 0) {
				printf ("ALREADY EXISTS A FILE WITH THE SAME NAME!\n");
				return NULL;
			}
		}		
	}
	
*/	
	// Resetting FirstFileBlock* file to fill it with the right block
	// Voyager is the block_num of the file in the blocklist	
	voyager = DiskDriver_getFreeBlock(disk, 0);
	if (voyager == ERROR_NO_FREE_BLOCKS) {
		printf ("ERROR : NO FREE BLOCKS AVAILABLE\n");
		return NULL;
	}
	snorlax = DiskDriver_readBlock(disk, file, voyager);
	if (!snorlax) {
		printf ("ERROR : SNORLAX IS BLOCKING THE WAY\n");
		return NULL;
	}
	
	memset(file, 0, sizeof(FirstFileBlock));
		
	// Filling the FirstFileBlock with the right structures
	file->header.previous_block = TBA;
	file->header.next_block = TBA;
	file->header.block_in_file = 0;

	file->fcb.directory_block = d->dcb->header.block_in_file;
	file->fcb.block_in_disk = voyager;
	strcpy(file->fcb.name, filename);
	file->fcb.size_in_bytes = sizeof(FirstFileBlock);
	file->fcb.size_in_blocks = 1;
	file->fcb.is_dir = FIL;
	strcpy(file->data, "\0");
	
	// Writing the file on the disk
	snorlax = DiskDriver_writeBlock(disk, file, voyager);
	if (snorlax == ERROR_FILE_WRITING) {
		printf ("ERROR : SNORLAX IS BLOCKING THE WAY\n");
		return NULL;
	}
	
	// Updating refs in DirectoryHandle's directory d->dcb
	d->dcb->file_blocks[d->dcb->num_entries] = voyager;
	++d->dcb->num_entries;
	
	// Updating the directory in disk, too
	int numblock = d->dcb->fcb.block_in_disk;
	snorlax = DiskDriver_writeBlock(disk, d->dcb, numblock);
	if (snorlax == ERROR_FILE_WRITING) {
		printf ("ERROR : SNORLAX IS BLOCKING THE WAY\n");
		return NULL;
	}
	
	// Filling the handle
	handle->sfs = d->sfs;
	handle->fcb = file;
	handle->directory = d->dcb;
	handle->current_block = &handle->fcb->header;
	handle->pos_in_file = 0;
	
	return handle;

	// WARNING : CAUSE OF THE BIG USE OF READ AND WRITES
	//           THIS OPERATION COULD BE DANGEROUS.
	//           WAITING TIME AND TESTING MORE TO IMPROVE THIS MECHANISM
}

// reads in the (preallocated) blocks array, the name of all files in a directory 
int SimpleFS_readDir(char** names, DirectoryHandle* d) {	
	int len = d->dcb->num_entries;
	int i = 0;
	FirstFileBlock f;
	
	while (i < len) {		
		DiskDriver_readBlock(d->sfs->disk, &f, d->dcb->file_blocks[i]);
		strcpy(names[i], f.fcb.name);
		++i;
	}

	return i;
}


// opens a file in the  directory d. The file should be exisiting
// returns NULL if the file does not exist
FileHandle* SimpleFS_openFile(DirectoryHandle* d, const char* filename) {

	// Checking for DirectoryHandle existance
	if (d == NULL) {
		return NULL;
	}
	
	DiskDriver* disk = d->sfs->disk;
	
	// Creating useful things
	FileHandle* handle = (FileHandle*) malloc(sizeof(FileHandle));
	FirstFileBlock* file = (FirstFileBlock*) malloc(sizeof(FirstFileBlock));
	
	// Checking for existing file	
	// Scanning all blocks in file_blocks: 
	// IF the block is occupied in the bitmap (voyager = 0)
	// && IF the scanned file's name == filename
	// MEANS THAT the searched file has ben found
	// SO we can create the handle
	for (int i = 0; i < d->dcb->num_entries; ++i) {
		int voyager = DiskDriver_readBlock(disk, file, d->dcb->file_blocks[i]);
		if (voyager == 0) {		
			if (strcmp(file->fcb.name, filename) == 0) {
				
				handle->sfs = d->sfs;
				handle->fcb = file;
				handle->directory = d->dcb;
				handle->current_block = &file->header;
				handle->pos_in_file = 0;
				
				return handle;
			}
		}		
	}
		
	return NULL;
	
}


// closes a file handle (destroyes it)
// RETURNS 0 on success, -1 if fails
int SimpleFS_close(FileHandle* f) {

	if (f == NULL) return -1;
	free(f->fcb);
	free(f);
	f = NULL;

	return 0; 
}

// writes in the file, at current position for size bytes stored in data
// overwriting and allocating new space if necessary
// returns the number of bytes written
int SimpleFS_write(FileHandle* f, void* data, int size);

// reads in the file, at current position size bytes stored in data
// overwriting and allocating new space if necessary
// returns the number of bytes read
int SimpleFS_read(FileHandle* f, void* data, int size);

// returns the number of bytes read (moving the current pointer to pos)
// returns pos on success
// -1 on error (file too short)
int SimpleFS_seek(FileHandle* f, int pos);

// seeks for a directory in d. If dirname is equal to ".." it goes one level up
// 0 on success, negative value on error
// it does side effect on the provided handle
 int SimpleFS_changeDir(DirectoryHandle* d, char* dirname);

// creates a new directory in the current one (stored in fs->current_directory_block)
// 0 on success
// -1 on error
int SimpleFS_mkDir(DirectoryHandle* d, char* dirname);

// removes the file in the current directory
// returns -1 on failure 0 on success
// if a directory, it removes recursively all contained files
int SimpleFS_remove(SimpleFS* fs, char* filename);
