#include "simplefs.h"
#include "bitmap.c"
#include "disk_driver.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Dimensione FirstDirectoryBlock
#define FDB_space (BLOCK_SIZE - sizeof(BlockHeader) - sizeof(FileControlBlock) - sizeof(int))/sizeof(int) 

// Dimensione DirectoryBlock
#define DB_space (BLOCK_SIZE - sizeof(BlockHeader))/sizeof(int)

// Dimensione linera FirstFileBlock
#define FFB_space BLOCK_SIZE - sizeof(FileControlBlock) - sizeof(BlockHeader)

// Dimensione libera FileBlock
#define FB_space BLOCK_SIZE - sizeof(BlockHeader)

// Funzione ausiliaria per verificare che il file non esista già nell dir corrente
int FileInDir(DiskDriver* disk, int space, int* file_blocks, const char* filename){
	FirstFileBlock ffb_aux;
	int i;
	for(i = 0; i < space; i++)
		if(file_blocks[i] > 0 && (DiskDriver_readBlock(disk, &ffb_aux, file_blocks[i])) != -1)
			if(!strncmp(ffb_aux.fcb.name, filename, 128))
				return i;
	return -1;
	// Ritorna il blocco i-esimo o -1 se non esiste
}

DirectoryHandle* SimpleFS_init(SimpleFS* fs, DiskDriver* disk){
	if(fs == NULL || disk == NULL){
		printf("ERRORE: parametri non validi");
		return NULL;
	}

	// Assegno il disco al FileSystem
	fs->disk = disk;
	
	FirstDirectoryBlock* fdb = malloc(sizeof(FirstDirectoryBlock));
	
	// Leggo il blocco 0 per la root, se c'è -> accanno
	int ret = DiskDriver_readBlock(disk, fdb, 0);
	if(ret == -1){
		free(fdb);
		return NULL;
	}
	
	// Else creo l'handle e lo restituisco
	DirectoryHandle* dh = (DirectoryHandle*) malloc(sizeof(DirectoryHandle));
	dh->sfs = fs;
	dh->dcb = fdb;
	dh->directory = fdb;
	dh->pos_in_block = 0;
	
	return dh;
}

void SimpleFS_format(SimpleFS* fs){
	if(fs==NULL){
		printf("ERRORE: parametri non validi");
		return;
	}
	
	memset(fs->disk->bitmap_data, 0, fs->disk->header->bitmap_entries);

	// Assegno i blocchi liberi
	fs->disk->header->free_blocks = fs->disk->header->num_blocks;
	fs->disk->header->first_free_block = 0;
	
	// Formatto la root
	FirstDirectoryBlock root = {0};
	root.header.block_in_file = 0;
	root.header.previous_block = -1;
	root.header.next_block = -1;
	
	root.fcb.directory_block = -1;
	root.fcb.block_in_disk = 0;
	root.fcb.is_dir = 1;
	root.fcb.size_in_blocks = 1;
	strcpy(root.fcb.name, "/");
	
	// Scrivo la root nel primo blocco (0)
	DiskDriver_writeBlock(fs->disk, &root, 0); 
}

FileHandle* SimpleFS_createFile(DirectoryHandle* d, const char* filename){
	if(d == NULL || filename == NULL){
		printf("ERRORE: parametri non validi");
		return NULL;
	}
	
	int ret;
	FirstDirectoryBlock* fdb = d->dcb;
	DiskDriver* disk = d->sfs->disk;
	
	// Un file o sta nel FDB o nel DB -> li controllo
	if(fdb->num_entries > 0){
		// Verifico che il file non esiste già nel FDB
		ret = FileInDir(disk, FDB_space, fdb->file_blocks, filename);
		if(ret >= 0){
			printf("ERRORE: Il file %s già esiste nel FDB\n", filename);
			return NULL;
		}
		
		DirectoryBlock db;
		
		// Verifico che non esista nel DB
		int next_block = fdb->header.next_block;
		while(next_block != -1){
			ret = DiskDriver_readBlock(disk, &db, next_block);
			if(ret < 0){
				printf("ERRORE: readBlock()");
				return NULL;
			}
			
			ret = FileInDir(disk, DB_space, db.file_blocks, filename);
			if(ret >= 0){
				printf("ERRORE: Il file %s già esiste nel DB\n", filename);
				return NULL;
			}
			next_block = db.header.next_block;
		}
	}
	
	// Mi sono asicurato che il file non esiste già
	// Prendo un blocco vuoto e procedo con la creazione
	int block_free = DiskDriver_getFreeBlock(disk, disk->header->first_free_block);
	if(block_free == -1){
		printf("ERRORE: Niente blocchi vuoti\n");
		return NULL;
	}
	
	FirstFileBlock* ffb_new = calloc(1, sizeof(FirstFileBlock));
	ffb_new->header.block_in_file = 0;
	ffb_new->header.previous_block = -1;
	ffb_new->header.next_block = -1;
	
	ffb_new->fcb.directory_block = fdb->fcb.block_in_disk;
	ffb_new->fcb.block_in_disk = block_free;
	ffb_new->fcb.is_dir = 0;
	ffb_new->fcb.size_in_bytes = 0;
	ffb_new->fcb.size_in_blocks = 1;

	strncpy(ffb_new->fcb.name, filename, 128);
	
	// Scrivo il file su disco
	ret = DiskDriver_writeBlock(disk, ffb_new, block_free);
	if(ret < 0){
		printf("ERRORE: writeBlock()\n");
		return NULL;
	}
	
	// Cerco posto in FDB o DB e poi salvo tutto
	int i = 0;																				
	int block_number = fdb->fcb.block_in_disk; // # blocco sul disco
	int entry = 0;	// # entry in file_blocks
	int put_in_DB = 0; // 0 -> fdb, 1 -> db

	int found = 0;
	int block_numer_dir = 0; // # blocco nella direcory
	int new_db_create = 0; // 0 -> no, 1 -> si
	int end_db_space = 0;

	// Se non c'è posto in FDB mi serve un DB
	DirectoryBlock db;

	// Posto in FDB
	if (fdb->num_entries < FDB_space){																
		int* blocks = fdb->file_blocks;
		for(i = 0; i < FDB_space; i++)																
			if (blocks[i] == 0){
				// Blocco libero trovato
				found = 1;
				entry = i;
				printf("trovato[%d] - ", entry);
				break;
			}
			end_db_space++;
	}
	else{																							
		put_in_DB = 1;
		int next_block = fdb->header.next_block;
		// Cerco una posizione libera in db
		while (next_block != -1 && !found){												
			ret = DiskDriver_readBlock(disk, &db, next_block);										
			if (ret == -1){
				printf("ERRORE: readBlock()\n");
				DiskDriver_freeBlock(disk, fdb->fcb.block_in_disk);									
				return NULL;
			}
			int* blocks = db.file_blocks;
			block_numer_dir++; 																		
			block_number = next_block;
			for(i = 0; i < DB_space; i++){									
				if (blocks[i] == 0){
					// Blocco libero trovato
					found = 1;
					entry = i;
					printf("trovato[%d] - ", entry);
					break;
				}
				end_db_space++;
			}
			new_db_create = 1;
			next_block = db.header.next_block;
		}
	}

	if(end_db_space > DB_space-1){
		printf("Spazio nel DB esaurito - %d\n", end_db_space);
		//return NULL;
	}

	// Nella transizione FDB -> DB quando devo allocare un file questo non trova un blocco libero
	// quindi creo un nuvo DirectoryBlock a cui fare riferimento
	// OSS: I successivi file nel DB faranno riferimento a questo blocco
	if(!found){
		printf("* Creo un db *\n");
		DirectoryBlock new = {0};
		new.header.next_block = -1;
		new.header.block_in_file = block_numer_dir;
		new.header.previous_block = block_number;
		new.file_blocks[0] = block_free;
		
		// Prendo un blocco lbero sulla quale allocare il nuovo db
		int new_db = DiskDriver_getFreeBlock(disk, disk->header->first_free_block);
		if(new_db == -1){
			printf("ERRORE: Niente blocchi liberi\n");
			DiskDriver_freeBlock(disk, fdb->fcb.block_in_disk);
			return NULL;
		}
		// Lo scrivo su disco
		ret = DiskDriver_writeBlock(disk, &new, new_db);
		if(ret == -1){
			printf("ERRORE: writeBlock()\n");
			DiskDriver_freeBlock(disk, fdb->fcb.block_in_disk);
			return NULL;
		}	
		
		if(new_db_create == 0) fdb->header.next_block = new_db;
		else db.header.next_block = new_db;
		
		db = new;
		block_number = new_db;
	}

	if (put_in_DB == 0){ // FDB
		printf("Salvo %s in FDB\n", filename);
		fdb->num_entries++;	
		fdb->file_blocks[entry] = block_free;				
		DiskDriver_updateBlock(disk, fdb, fdb->fcb.block_in_disk);
	}
	 else{ // DB
		printf("Salvo %s in DB\n", filename);
		fdb->num_entries++;	
		DiskDriver_updateBlock(disk, fdb, fdb->fcb.block_in_disk);
		db.file_blocks[entry] = block_free;
		DiskDriver_updateBlock(disk, &db, block_number);
	}

	// Creo e restituisco il file handle
	FileHandle* fh = malloc(sizeof(FileHandle));
	fh->sfs = d->sfs;
	fh->fcb = ffb_new;
	fh->directory = fdb;
	fh->pos_in_file = 0;
	
	return fh;
}

int SimpleFS_close(FileHandle* f){
	if(f == NULL) return -1;
	free(f->fcb);
	free(f);
	return 0;
}

int SimpleFS_readDir(char** names, DirectoryHandle* d){
	if (d == NULL || names == NULL){
		printf("ERRORE: parametri non validi");
		return -1;
	}

	int ret, n = 0, i;
	FirstDirectoryBlock* fdb = d->dcb;
	DiskDriver* disk = d->sfs->disk;

	if (fdb->num_entries > 0){
		FirstFileBlock ffb; 

		int* blocks = fdb->file_blocks;
		for(i = 0; i < FDB_space; i++){
			if(blocks[i] < 0) continue;
			ret = DiskDriver_readBlock(disk, &ffb, blocks[i]);
			if(blocks[i] > 0 && ret != -1){
				// Copio ul nome in names
				names[n] = strndup(ffb.fcb.name, 128);
				n++;
			}
		}
		
		DirectoryBlock db;
		
		// Devo verificare se ci sono files anche in altri DB
		if(i < fdb->num_entries){
			int next_block = fdb->header.next_block;
			while(next_block != -1){
				ret = DiskDriver_readBlock(disk, &db, next_block);
				if(ret != 0){
					printf("ERRORE: readBlock()");
					return -1;
				}
				int* blocks = db.file_blocks;
				for(i = 0; i < DB_space; i++){
					// Leggo il FirstFileBlock del file che sto verificando
					ret = DiskDriver_readBlock(disk, &ffb, blocks[i]);
					if(ret != 0){
						printf("ERRORE: readBlock()");
						return -1;
					}
					if(blocks[i] > 0 && ret != -1){ 
						names[n] = strndup(ffb.fcb.name, 128);
						n++;
					}
				}
				next_block = db.header.next_block;
			}
		}
	}
	return 0;
}

static int dirInDIr(DiskDriver* disk, int space, int* file_blocks, const char* filename){
	FirstDirectoryBlock fdb;
	int i;
	
	for(i = 0; i < space; i++)
		if(file_blocks[i] > 0 && DiskDriver_readBlock(disk, &fdb, file_blocks[i]) != -1)
			if(!strncmp(fdb.fcb.name, filename, 128))
				return i;

	return -1;
}
		
int SimpleFS_mkDir(DirectoryHandle* d, char* dirname){
	if(d == NULL || dirname == NULL){
		printf("ERRORE: parametri non validi");
		return -1;
	}
	
	int ret;
	FirstDirectoryBlock* fdb = d->dcb;
	DiskDriver* disk = d->sfs->disk;
	
	// Un file o sta nel FDB o nel DB -> li controllo
	if(fdb->num_entries > 0){
		// Verifico che il file non esiste già nel FDB
		ret = FileInDir(disk, FDB_space, fdb->file_blocks, dirname);
		if(ret >= 0){
			printf("ERRORE: Il file %s già esiste nel FDB\n", dirname);
			return -1;
		}
		
		DirectoryBlock db;
		
		// Verifico che non esista nel DB
		int next_block = fdb->header.next_block;
		while(next_block != -1){
			ret = DiskDriver_readBlock(disk, &db, next_block);
			if(ret < 0){
				printf("ERRORE: readBlock()");
				return -1;
			}
			
			ret = FileInDir(disk, DB_space, db.file_blocks, dirname);
			if(ret >= 0){
				printf("ERRORE: Il file %s già esiste nel DB\n", dirname);
				return -1;
			}
			next_block = db.header.next_block;
		}
	}
	
	// Mi sono asicurato che il file non esiste già
	// Prendo un blocco vuoto e procedo con la creazione
	int block_free = DiskDriver_getFreeBlock(disk, disk->header->first_free_block);
	if(block_free == -1){
		printf("ERRORE: Niente blocchi vuoti\n");
		return -1;
	}
	
	FirstDirectoryBlock* fdb_new = calloc(1, sizeof(FirstFileBlock));

	fdb_new->header.block_in_file = 0;
	fdb_new->header.next_block = -1;
	fdb_new->header.previous_block = -1;

	fdb_new->fcb.directory_block = fdb->fcb.block_in_disk;
	fdb_new->fcb.block_in_disk = block_free;
	fdb_new->fcb.is_dir = 1;

	strcpy(fdb_new->fcb.name, dirname);
	
	// Scrivo il file su disco
	ret = DiskDriver_writeBlock(disk, fdb_new, block_free);
	if(ret < 0){
		printf("ERRORE: writeBlock()\n");
		return -1;
	}
	
	// Cerco posto in FDB o DB e poi salvo tutto
	int i = 0;																				
	int block_number = fdb->fcb.block_in_disk; // # blocco sul disco
	int entry = 0;	// # entry in file_blocks
	int put_in_DB = 0; // 0 -> fdb, 1 -> db

	int found = 0;
	int block_numer_dir = 0; // # blocco nella direcory
	int new_db_create = 0; // 0 -> no, 1 -> si
	int end_db_space = 0;

	// Se non c'è posto in FDB mi serve un DB
	DirectoryBlock db;

	// Posto in FDB
	if (fdb->num_entries < FDB_space){																
		int* blocks = fdb->file_blocks;
		for(i = 0; i < FDB_space; i++)																
			if (blocks[i] == 0){
				// Blocco libero trovato
				found = 1;
				entry = i;
				printf("trovato[%d] - ", entry);
				break;
			}
			end_db_space++;
	}
	else{																							
		put_in_DB = 1;
		// Cerco una posizione libera in db
		int next_block = fdb->header.next_block;
		while (next_block != -1 && !found){												
			ret = DiskDriver_readBlock(disk, &db, next_block);										
			if (ret == -1){
				printf("ERRORE: readBlock()\n");
				DiskDriver_freeBlock(disk, fdb->fcb.block_in_disk);									
				return -1;
			}
			int* blocks = db.file_blocks;
			block_numer_dir++; 																		
			block_number = next_block;
			for(i = 0; i < DB_space; i++){									
				if (blocks[i] == 0){
					// Blocco libero trovato
					found = 1;
					entry = i;
					printf("trovato[%d] - ", entry);
					break;
				}
				end_db_space++;
			}
			new_db_create = 1;
			next_block = db.header.next_block;
		}
	}

	if(end_db_space > DB_space-1){
		printf("Spazio nel DB esaurito - %d\n", end_db_space);
		//return NULL;
	}

	// Nella transizione FDB -> DB quando devo allocare un file questo non trova un blocco libero
	// quindi creo un nuvo DirectoryBlock a cui fare riferimento
	// OSS: I successivi file nel DB faranno riferimento a questo blocco
	if(!found){
		printf("* Creo un db *\n");
		DirectoryBlock new = {0};
		new.header.next_block = -1;
		new.header.block_in_file = block_numer_dir;
		new.header.previous_block = block_number;
		new.file_blocks[0] = block_free;
		
		// Prendo un blocco lbero sulla quale allocare il nuovo db
		int new_db = DiskDriver_getFreeBlock(disk, disk->header->first_free_block);
		if(new_db == -1){
			printf("ERRORE: Niente blocchi liberi\n");
			DiskDriver_freeBlock(disk, fdb->fcb.block_in_disk);
			return -1;
		}
		// Lo scrivo su disco
		ret = DiskDriver_writeBlock(disk, &new, new_db);
		if(ret == -1){
			printf("ERRORE: writeBlock()\n");
			DiskDriver_freeBlock(disk, fdb->fcb.block_in_disk);
			return -1;
		}	
		
		if(new_db_create == 0) fdb->header.next_block = new_db;
		else db.header.next_block = new_db;
		
		db = new;
		block_number = new_db;
	}

	if (put_in_DB == 0){ // FDB
		printf("Salvo %s in FDB\n", dirname);
		fdb->num_entries++;	
		fdb->file_blocks[entry] = block_free;				
		DiskDriver_updateBlock(disk, fdb, fdb->fcb.block_in_disk);
	}
	else{ // DB
		printf("Salvo %s in DB\n", dirname);
		fdb->num_entries++;	
		DiskDriver_updateBlock(disk, fdb, fdb->fcb.block_in_disk);
		db.file_blocks[entry] = block_free;
		DiskDriver_updateBlock(disk, &db, block_number);
	}

	/*
	// Creo e restituisco il file handle
	FileHandle* fh = malloc(sizeof(FileHandle));
	fh->sfs = d->sfs;
	fh->fcb = fdb_new;
	fh->directory = fdb;
	fh->pos_in_file = 0;
	
	return fh;
	*/
	return 0;
}


int SimpleFS_changeDir(DirectoryHandle* d, char* dirname){
	if(d == NULL || dirname == NULL){
		printf("ERRORE: parametri non validi");
		return -1;
	}

	int ret;
	// Vado da mio padre?
	if(!strncmp(dirname, "..", 2)){
		// Sono la root?
		if(d->dcb->fcb.block_in_disk == 0){
			printf("ERRORE: Sono la root\n");
			return -1;
		}
		
		d->pos_in_block = 0; // absolute position -> reset
		d->dcb = d->directory; // pointer primo blovvo al padre

		int parent_block = d->dcb->fcb.directory_block;
		if(parent_block == -1){ // è la root
			d->directory = NULL;
			return 0;
		}
		
		FirstDirectoryBlock* parent = malloc(sizeof(FirstDirectoryBlock));
		// Leggo papà
		ret = DiskDriver_readBlock(d->sfs->disk, parent, parent_block);
		if(ret == -1){
			printf("ERRORE: readBLock()\n");
			d->directory = NULL;
		} 
		else d->directory = parent; // Lo salvo

		return 0;
	} 
	else if(d->dcb->num_entries < 0){
		printf("ERRORE: la dir è vuota\n");
		return -1;
	}
	
	// else try to enter
	FirstDirectoryBlock* fdb = d->dcb;
	DiskDriver* disk = d->sfs->disk;
	
	FirstDirectoryBlock* fdb_search = malloc(sizeof(FirstDirectoryBlock));
	
	//La dir si trova in FDB?
	int pos = dirInDIr(disk, FDB_space, fdb->file_blocks, dirname);
	if(pos >= 0){
		DiskDriver_readBlock(disk, fdb_search, fdb->file_blocks[pos]);
		d->pos_in_block = 0;
		d->directory = fdb;
		d->dcb = fdb_search;
		return 0;
	}
	
	DirectoryBlock db;
	int next_block = fdb->header.next_block; 
	while(next_block != -1){
		ret = DiskDriver_readBlock(disk, &db, next_block);
		if(ret == -1){
			printf("ERRORE: readBlock\n");
			return -1;
		}
		pos = dirInDIr(disk, DB_space, db.file_blocks, dirname);
		if(pos >= 0){
			DiskDriver_readBlock(disk, fdb_search, db.file_blocks[pos]);
			d->directory = fdb;
			d->dcb = fdb_search;
			return 0;
		}
		next_block = db.header.next_block;
	}
	
	printf("La dir non esiste\n");
	return -1;
}

void print_path(DiskDriver* disk, FirstDirectoryBlock* fdb, int block_in_disk){
	if(block_in_disk == -1) return;

	DiskDriver_readBlock(disk, fdb, block_in_disk);
	block_in_disk = fdb->fcb.directory_block;
	char dirname[128];
	strncpy(dirname, fdb->fcb.name, 128);
	
	print_path(disk, fdb, block_in_disk);

	if(strcmp(dirname, "/") == 0) printf("root");
	else printf("/%s", dirname);
}

FileHandle* SimpleFS_openFile(DirectoryHandle* d, const char* filename){
	if(d == NULL || filename == NULL){
		printf("ERRORE: parametri non validi");
		return NULL;
	}

	int ret;
	FirstDirectoryBlock* fdb = d->dcb;
	DiskDriver* disk = d->sfs->disk;
	
	// Se la dir non è vuota
	if(fdb->num_entries > 0){
		FileHandle* fh = malloc(sizeof(FileHandle));
		fh->sfs = d->sfs;
		fh->directory = fdb;
		fh->pos_in_file = 0;
		
		int found;
		FirstFileBlock* ffb_open = malloc(sizeof(FirstFileBlock));
		
		// In FDB
		int pos = FileInDir(disk, FDB_space, fdb->file_blocks, filename);
		if(pos >= 0){
			found = 1;
			DiskDriver_readBlock(disk, ffb_open, fdb->file_blocks[pos]);
			fh->fcb = ffb_open;
		}
		
		DirectoryBlock db;
		
		// In DB
		int next_block = fdb->header.next_block;
		while(next_block != -1 && !found){
			ret = DiskDriver_readBlock(disk, &db, next_block);
			pos = FileInDir(disk, DB_space, db.file_blocks, filename);
			if(pos >= 0){
				found = 1;
				DiskDriver_readBlock(disk, ffb_open, db.file_blocks[pos]);
				fh->fcb = ffb_open;
			}
			next_block = db.header.next_block;
		}
		
		if(found) return fh;
		else{
			printf("ERRORE: file non trovato\n");
			free(fh);
		}
	}
	else printf("ERRORE: dir vuota\n");

	return NULL;
}

int SimpleFS_write(FileHandle* f, void* data, int size){
	FirstFileBlock* ffb = f->fcb;

	int ret;
	int written_bytes = 0;
	int bytes_rem = size;
	int pos_cursore = f->pos_in_file; // pos cursore
	
	// In FFB
	if(pos_cursore < FFB_space && bytes_rem <= FFB_space - pos_cursore){
		memcpy(ffb->data + pos_cursore, (char*)data, bytes_rem);
		written_bytes += bytes_rem;
		if(f->pos_in_file + written_bytes > ffb->fcb.size_in_bytes)
			ffb->fcb.size_in_bytes = f->pos_in_file + written_bytes;
		DiskDriver_updateBlock(f->sfs->disk, ffb, ffb->fcb.block_in_disk);

		return written_bytes;
	}
	else if(pos_cursore < FFB_space && bytes_rem > FFB_space - pos_cursore){
		memcpy(ffb->data + pos_cursore, (char*)data, FFB_space - pos_cursore);
		written_bytes += FFB_space - pos_cursore;
		bytes_rem = size - written_bytes;
		DiskDriver_updateBlock(f->sfs->disk, ffb, ffb->fcb.block_in_disk);
		pos_cursore = 0;
	}
	 
	int block_in_disk = ffb->fcb.block_in_disk; // blocco corrente 
	int next_block = ffb->header.next_block;
	int block_in_file = ffb->header.block_in_file;

	FileBlock fb_aux;
	int one_block = 0;
	if(next_block == -1) one_block = 1;
	 
	// In FB
	int fb_block_i = 1;
	while(written_bytes < size){
		printf("Uso il blocco: %d\n", fb_block_i++);
		// blocco da creare
		if(next_block == -1){
			FileBlock new = {0};
			new.header.block_in_file = block_in_file + 1;
			new.header.next_block = -1;
			new.header.previous_block = block_in_disk;
			 
			ffb->fcb.size_in_blocks += 1;

			// prendo il primo blocco libero
			next_block = DiskDriver_getFreeBlock(f->sfs->disk, block_in_disk);
			if(one_block == 1){ // Non ci sono altri blocchi allocati dopo
				 ffb->header.next_block = next_block;
				 DiskDriver_updateBlock(f->sfs->disk, ffb, ffb->fcb.block_in_disk);
				 one_block = 0;
				 printf("No blocks\n");
			}
			else{ // Aggiorno il blocco successivo
				printf("Updating\n");
				fb_aux.header.next_block = next_block;
				DiskDriver_updateBlock(f->sfs->disk, &fb_aux, block_in_disk);
			}
			DiskDriver_writeBlock(f->sfs->disk, &new, next_block);
			 
			fb_aux = new;
		}
		// esiste e lo leggo
		else{
			ret = DiskDriver_readBlock(f->sfs->disk, &fb_aux, next_block);
			if(ret == -1) return -1;
		}
		
		if(pos_cursore < FB_space && bytes_rem <= FB_space - pos_cursore){
			memcpy(fb_aux.data + pos_cursore, (char*)data + written_bytes, bytes_rem);
			written_bytes += bytes_rem;
			if(f->pos_in_file + written_bytes > ffb->fcb.size_in_bytes)
				ffb->fcb.size_in_bytes = f->pos_in_file + written_bytes;
			DiskDriver_updateBlock(f->sfs->disk, ffb, ffb->fcb.block_in_disk);
			DiskDriver_updateBlock(f->sfs->disk, &fb_aux, next_block);
			
			return written_bytes;
		}
		else if(pos_cursore < FB_space && bytes_rem > FB_space - pos_cursore){
			memcpy(fb_aux.data + pos_cursore, (char*)data + written_bytes, FB_space - pos_cursore);
			written_bytes += FB_space - pos_cursore;
			bytes_rem = size - written_bytes;
			DiskDriver_updateBlock(f->sfs->disk, &fb_aux, next_block);
			pos_cursore = 0;
		}
		 
		block_in_disk = next_block;
		next_block = fb_aux.header.next_block;
		block_in_file = fb_aux.header.block_in_file;
	}

	return written_bytes;
 }
	
	
int SimpleFS_read(FileHandle* f, void* data, int size){
	FirstFileBlock* ffb = f->fcb;
	
	int pos_cursore = f->pos_in_file; // pos cursore
	int written_bytes = ffb->fcb.size_in_bytes;
	
	if(size + pos_cursore > written_bytes){
		printf(">limit\n");
		memset(data, 0, size);
		return -1;
	}
	
	int bytes_read = 0;
	int bytes_rem = size;
	
	//In FFB
	if(pos_cursore < FFB_space && bytes_rem <= FFB_space-pos_cursore){
		memcpy(data, ffb->data+pos_cursore, bytes_rem);
		bytes_read += bytes_rem;
		bytes_rem = size - bytes_read;
		f->pos_in_file += bytes_read; // aggiorno il cursore
		return bytes_read;
	}
	// Se devo leggere piu del FFB_space
	else if(pos_cursore < FFB_space && bytes_rem > FFB_space - pos_cursore){
		memcpy(data, ffb->data+pos_cursore, FFB_space-pos_cursore);
		bytes_read += FFB_space-pos_cursore;
		bytes_rem = size - bytes_read;
		pos_cursore = 0;
	}
	
	FileBlock fb_aux;

	// In FB 
	if(bytes_read < size && ffb->header.next_block != -1){
		DiskDriver_readBlock(f->sfs->disk, &fb_aux, ffb->header.next_block);
		if(pos_cursore < FB_space && bytes_rem <= FB_space - pos_cursore){
			memcpy(data + bytes_read, fb_aux.data + pos_cursore, bytes_rem);
			bytes_read += bytes_rem;
			bytes_rem = size-bytes_read;
			f->pos_in_file += bytes_read;
			return bytes_read;
		}
		else if(pos_cursore < FB_space && bytes_rem > FB_space - pos_cursore){
			memcpy(data + bytes_read, fb_aux.data + pos_cursore, FB_space - pos_cursore);
			bytes_read += FB_space-pos_cursore;
			bytes_rem = size-bytes_read;
			pos_cursore = 0;
		}
	}

	return bytes_read;
}

int SimpleFS_seek(FileHandle* f, int pos){
	if(pos < 0){
		printf("ERRORE: Parametri non validi");
		return -1;
	}

	FirstFileBlock* ffb = f->fcb;
	
	if(pos > ffb->fcb.size_in_bytes){
		printf("ERRORE: out size\n");
		return -1;
	}
	
	f->pos_in_file = pos;
	return pos;
} 

int SimpleFS_remove(DirectoryHandle* d, char* filename){
	if(d == NULL || filename == NULL){
		printf("ERRORE: parametri non validi");
		return -1;
	}
	FirstDirectoryBlock* fdb = d->dcb;
	int ret;

	int pos_file = FileInDir(d->sfs->disk, FDB_space, fdb->file_blocks, filename);
	int file_in_fdb = 1;

	DirectoryBlock* db_tmp = (DirectoryBlock*)malloc(sizeof(DirectoryBlock));
	int next_block = fdb->header.next_block;
	int block_in_disk = fdb->fcb.block_in_disk;
	
	// Entra se il file non si trova nel fdb quindi
	while(pos_file == -1){
		if(next_block != -1){ // COntrollo tutti i blocchi della stessa dir
			file_in_fdb = 0;
			ret = DiskDriver_readBlock(d->sfs->disk, db_tmp, next_block);
			if(ret == -1){
				printf("ERRORE: readBlock()\n");
				return -1;
			}
			pos_file = FileInDir(d->sfs->disk, DB_space, db_tmp->file_blocks, filename);
			block_in_disk = next_block;
			next_block = db_tmp->header.next_block;
		}
		else //blocchi finiti
			return -1;
	}
	
	int block_check;
	if(file_in_fdb == 0) block_check = db_tmp->file_blocks[pos_file];
	else block_check = fdb->file_blocks[pos_file];
	
	FirstFileBlock ffb_rm;
	ret = DiskDriver_readBlock(d->sfs->disk, &ffb_rm, block_check);
	if(ret == -1){
		printf("ERRORE: readBlock\n");
		return -1;
	}

	if(ffb_rm.fcb.is_dir == 0){ // sto eliminando un file
		FileBlock fb_tmp;
		int next = ffb_rm.header.next_block;
		int block_in_disk = block_check;
		while(next != -1){
			ret = DiskDriver_readBlock(d->sfs->disk, &fb_tmp, next);
			if(ret == -1){
				printf("ERRORE: readBlock\n");
				return -1;
			}
			block_in_disk = next;
			next = fb_tmp.header.next_block;
			DiskDriver_freeBlock(d->sfs->disk, block_in_disk);
		}
		DiskDriver_freeBlock(d->sfs->disk, block_check);
		d->dcb = fdb;
		ret = 0;
	}
	else{ // sto eliminando una dir
		FirstDirectoryBlock fdb_rm;
		ret = DiskDriver_readBlock(d->sfs->disk, &fdb_rm, block_check);
		if(ret == -1){
			printf("ERRORE: readBlock\n");
			return -1;
		}

		if(fdb_rm.num_entries > 0){
			ret = SimpleFS_changeDir(d, fdb_rm.fcb.name);
			if(ret == -1){
				printf("ERRORE: changeDir()\n");
				return -1;
			}
			int i;
			// FDB -> Elimino tutti i file nella dir
			for(i = 0; i < FDB_space; i++){
				FirstFileBlock ffb;
				ret = DiskDriver_readBlock(d->sfs->disk, &ffb, fdb_rm.file_blocks[i]);
				if(fdb_rm.file_blocks[i] > 0 && ret != -1)
					SimpleFS_remove(d, ffb.fcb.name);
			}

			// DB -> Elimino tutti i file nella dir
			int next = fdb_rm.header.next_block;
			int block_in_disk = block_check;
			DirectoryBlock db_tmp;
			while(next != -1){
				ret = DiskDriver_readBlock(d->sfs->disk, &db_tmp, next);
				if(ret == -1){
					printf("ERRORE: readBlock()\n");
					return -1;
				}
				int j;
				for(j = 0; j < DB_space; j++){
					FirstFileBlock ffb;
					ret = DiskDriver_readBlock(d->sfs->disk, &ffb, db_tmp.file_blocks[j]);
					if(ret == -1){
						printf("ERRORE: readBlock()\n");
						return -1;
					}
					SimpleFS_remove(d, ffb.fcb.name);
				}
				block_in_disk = next;
				next = db_tmp.header.next_block;
				DiskDriver_freeBlock(d->sfs->disk, block_in_disk);
			}

			DiskDriver_freeBlock(d->sfs->disk, block_check);
			d->dcb = fdb;
			ret = 0;
		}
		else{
			DiskDriver_freeBlock(d->sfs->disk, block_check);
			d->dcb = fdb;
			ret = 0;
		}
	}
	
	if(file_in_fdb){
		fdb->file_blocks[pos_file] =- 1;
		fdb->num_entries -= 1;
		DiskDriver_updateBlock(d->sfs->disk, fdb, fdb->fcb.block_in_disk);
		free(db_tmp);
		return ret;
	}
	else{
		db_tmp->file_blocks[pos_file] = -1;
		fdb->num_entries = -1;
		DiskDriver_updateBlock(d->sfs->disk, db_tmp, block_in_disk);
		DiskDriver_updateBlock(d->sfs->disk, fdb, fdb->fcb.block_in_disk);
		free(db_tmp);
		return ret;
	}
	
	return -1;
}

int main(int agc, char** argv) {

    // *** Bitmap test ***
	printf("\n\n\n\n*** BITMAP ***\n\n");
	printf("Creo una bitmap con 3 entries: 69, 128, 1");
	BitMap* bmp=(BitMap*)malloc(sizeof(BitMap));
	bmp->num_bits = 24;
	bmp->entries = (char*)malloc(sizeof(char)*3);
	bmp->entries[0] = 69;
	bmp->entries[1] = 128;
	bmp->entries[2] = 1;
	
	printf("\nEntries[0] = ");
	BitMap_print(bmp, 0);
	printf("\nEntries[1] = ");
	BitMap_print(bmp, 1);
	printf("\nEntries[2] = ");
	BitMap_print(bmp, 2);
	printf("\n\nPrendo il primo bit a 0, a partire dalla posizione 7\n");
	printf("Risultato = %d [CORRETTO = 7]\n", BitMap_get(bmp, 7, 0));

	printf("\nSetto questo bit a 1\n");
	BitMap_set(bmp, 7, 1);
	printf("Risultato = ");
	BitMap_print(bmp, 0);
	printf(" [CORRETTO = 11000101]\n");
	
	printf("\nPrendo il primo bit a 1, a partire dalla posizione 8\n");
	printf("Risultato = %d [Corretto = 15]\n", BitMap_get(bmp, 8, 1));	
	free(bmp->entries);
	free(bmp);
	

	// *** DiskDriver test ***
  	DiskDriver disk;

	printf("\n\n\n\n*** DISK DRIVER ***\n\n");
	printf("Creo un disco con 3 blocchi: test.txt");

	DiskDriver_init(&disk, "test.txt", 3);
	printf("\n%d blocchi liberi\n\n", disk.header->free_blocks);

	// h -> Block header all'inizio di ogni blocco del disco	
	BlockHeader h;
	
	int i;
	char txt[BLOCK_SIZE - sizeof(BlockHeader)];

	FileBlock* fb1 = (FileBlock*)malloc(sizeof(FileBlock));
	fb1->header = h;
	for(i = 0; i < BLOCK_SIZE - sizeof(BlockHeader); i++)
		txt[i] = '0';
	strcpy(fb1->data, txt);
	
	FileBlock* fb2 = (FileBlock*)malloc(sizeof(FileBlock));
	fb2->header = h;
	for(i = 0; i < BLOCK_SIZE - sizeof(BlockHeader); i++)
		txt[i] = '1';
	strcpy(fb2->data, txt);
	
	FileBlock* fb3 = (FileBlock*)malloc(sizeof(FileBlock));
	fb3->header = h;
	for(i = 0; i < BLOCK_SIZE - sizeof(BlockHeader); i++)
		txt[i] = '2';
	strcpy(fb3->data, txt);
	
	// Scrittura su disco
	printf(" * Scrittura *\n\n");
	
	printf("Scrittura sul blocco 0\n");
	DiskDriver_writeBlock(&disk, fb1, 0);
	DiskDriver_flush(&disk);
	printf("Blocchi liberi: %d\nPrimo blocco libero: %d\n\n", disk.header->free_blocks, disk.header->first_free_block);
	
	printf("Scrittura sul blocco 1\n");
	DiskDriver_writeBlock(&disk, fb2, 1);
	DiskDriver_flush(&disk);
	printf("Blocchi liberi: %d\nPrimo blocco libero: %d\n\n", disk.header->free_blocks, disk.header->first_free_block);
	
	printf("Scrittura sul blocco 2\n");
	DiskDriver_writeBlock(&disk, fb3, 2);
	DiskDriver_flush(&disk);
	printf("Blocchi liberi: %d\nPrimo blocco libero: %d\n\n", disk.header->free_blocks, disk.header->first_free_block);
	
	// Leggo i blocchi che ho scritto
	printf(" * Lettura *\n\n");

	FileBlock* fb_read = (FileBlock*)malloc(sizeof(FileBlock));

	DiskDriver_readBlock(&disk, fb_read, 0);
	printf("Lettura del blocco 0: %s\n\n", fb_read->data);

	DiskDriver_readBlock(&disk, fb_read, 1);
	printf("Lettura del blocco 1: %s\n\n", fb_read->data);

	DiskDriver_readBlock(&disk, fb_read, 2);
	printf("Lettura del blocco 2: %s\n\n", fb_read->data);

	// Rilascio
	printf(" * Rilascio delle risorse *\n\n");

	DiskDriver_freeBlock(&disk, 0);
	DiskDriver_freeBlock(&disk, 1);
	DiskDriver_freeBlock(&disk, 2);
	printf("Blocchi liberi: %d\n\n", disk.header->free_blocks);
	
	free(fb1);
	free(fb2);
	free(fb3);
	free(fb_read);

	//Elimino il disco dal file system
	int ret = remove("test.txt");
	if(ret < 0){
		printf("ERRORE: remove()");
		return -1;
	}

	//simplefs
	printf("\n*** SIMPLEFS ***\n");
	printf("Creo un nuovo disco di 128 blocchi e il FileSystem\n");
	printf("Spazio nel FDB: %ld\nSpazio nel DB: %ld", FDB_space, DB_space);
	DiskDriver disk2;
	SimpleFS sfs;
	DiskDriver_init(&disk2, "test2.txt", 128);
	DiskDriver_flush(&disk2);
	DirectoryHandle* dh = SimpleFS_init(&sfs, &disk2);
	if(dh == NULL){
		SimpleFS_format(&sfs);
		dh = SimpleFS_init(&sfs, &disk2);
		if(dh == NULL) exit(EXIT_FAILURE);
	}
	
	// Creo 130 file su un blocco da 128-header
	printf("\n\n * Creo 130 file * \n");
	char filename[10];
	FileHandle* fh = NULL;
	for(i = 0; i < 110; i++){
		sprintf(filename, "%d", i);
		fh = SimpleFS_createFile(dh, filename);
	}
	if(fh != NULL) SimpleFS_close(fh);

	// Li leggo
	printf("\n * Leggo la directory corrente (root) * \n");
	char** files = (char**) malloc(sizeof(char*) * dh->dcb->num_entries);
	ret = SimpleFS_readDir(files, dh);
	for (i=0; i<dh->dcb->num_entries; i++)
		printf("%s ", files[i]);
	
	for (i=0; i<dh->dcb->num_entries; i++)
		free(files[i]);
	
	free(files);
	printf("\n\n");

	printf(" * Creo una directory d1 *\n\n");
	SimpleFS_mkDir(dh, "d1");
	if(dh == NULL) exit(EXIT_FAILURE);
	
	FirstDirectoryBlock* fdb = (FirstDirectoryBlock*)malloc(sizeof(FirstDirectoryBlock));
	
	ret = SimpleFS_readDir(files, dh);
	for (i = 0; i < dh->dcb->num_entries; i++)
		printf("%s ", files[i]);
	for (i = 0; i < dh->dcb->num_entries; i++)
		free(files[i]);

	printf("\n\n");

	printf(" * Creo di nuovo una directory d1 *\n");
	SimpleFS_mkDir(dh, "d1");
	if(dh == NULL) exit(EXIT_FAILURE);

	printf("\n * Entro in d1 *\n");
	SimpleFS_changeDir(dh, "d1");
	printf("Path: ");
	print_path(dh->sfs->disk, fdb, dh->dcb->fcb.block_in_disk);
	printf("\n");

	fh = NULL;
	for(i = 0; i < 7; i++){
		sprintf(filename, "%d", i);
		fh = SimpleFS_createFile(dh, filename);
	}
	if(fh != NULL) SimpleFS_close(fh);

	printf("\n * Esco da d1 *\n");
	SimpleFS_changeDir(dh, "..");
	printf("Path: ");
	print_path(dh->sfs->disk, fdb, dh->dcb->fcb.block_in_disk);
	free(fdb);

	printf("\n\n");

	printf("\n * Creo un file per fare i test read/write/seek * \n");
	FileHandle* fh_test = SimpleFS_createFile(dh, "file_test.txt");
	if(fh_test == NULL) exit(EXIT_FAILURE);
	else{
		FileHandle* f = SimpleFS_openFile(dh, "file_test.txt");
		printf("\n * Apertura del file di prova *\n");
		if(f == NULL) printf("ERRORE: il file non esiste\n");

		printf("\n * Scrittura del carattere '7' per 800 volte * '\n");
		// testo il wtite in FB
		char txt[800];
		for(i = 0; i < 800; i++){
			if(i%100 == 0) txt[i]='\n';
			else txt[i] = '7';
		}
		
		SimpleFS_write(f, txt, 800);

		// In realtà qui sovrascrivo quindi
		printf("\nCi sovrascrivo 'Questo è un file di prova, ora ne manipolo i dati'\n");

		SimpleFS_write(f, "Questo è un file di prova, ora ne manipolo i dati", 50);
		printf("\n\nLeggo il file di prova:\n");

		int size = 800;
		char* data = (char*)malloc(sizeof(char)*size+1);
		data[size] = '\0';
		SimpleFS_read(f, data, size);
		printf("%s", data);
		free(data);

		SimpleFS_seek(f, 0);

		printf("\n\n * Leggo i primi 26 bytes *:\n");

		size = 26;
		data = (char*)malloc(sizeof(char)*size+1);
		data[size] = '\0';
		SimpleFS_read(f, data, size);
		printf("%s\n\n\n", data);
		free(data);

		SimpleFS_close(fh_test);
	}

	printf(" * Rimuovo i primi tre file *\n");

	for(i = 0; i < 3; i++){
		sprintf(filename, "%d", i);
		SimpleFS_remove(dh, filename);
	}

	ret = SimpleFS_readDir(files, dh);
	for (i = 0; i < dh->dcb->num_entries; i++)
		printf("%s ", files[i]);
	for (i = 0; i < dh->dcb->num_entries; i++)
		free(files[i]);

	free(files);


	printf("\n\n");

	SimpleFS_format(&sfs);

}
