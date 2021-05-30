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
		if(fdb->header.next_block != -1){
			ret = DiskDriver_readBlock(disk, &db, fdb->header.next_block);
			if(ret < 0){
				printf("ERRORE: readBlock()");
				return NULL;
			}
			
			ret = FileInDir(disk, DB_space, db.file_blocks, filename);
			if(ret >= 0){
				printf("ERRORE: Il file già esiste nel DB\n");
				return NULL;
			}
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
		int next = fdb->header.next_block;
		// Cerco una posizione libera in db
		if (fdb->header.next_block != -1 && !found){												
			ret = DiskDriver_readBlock(disk, &db, fdb->header.next_block);										
			if (ret == -1){
				printf("ERRORE: readBlock()\n");
				DiskDriver_freeBlock(disk, fdb->fcb.block_in_disk);									
				return NULL;
			}
			int* blocks = db.file_blocks;
			block_numer_dir++; 																		
			block_number = fdb->header.next_block;
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
			if(fdb->header.next_block != -1){
				ret = DiskDriver_readBlock(disk, &db, fdb->header.next_block);
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
			}
		}
	}
	return 0;
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
	BitMap_set(bmp, 4, 1);
	printf("Risultato = ");
	BitMap_print(bmp, 0);
	printf(" [CORRETTO = 01010101]\n");
	
	printf("\nPrendo il primo bit a 1, a partire dalla posizione 7\n");
	printf("Risultato = %d [Corretto = 15]\n", BitMap_get(bmp, 7, 1));	
	free(bmp->entries);
	free(bmp);
	

	// *** DiskDriver test ***
  	DiskDriver disk;

	printf("\n\n\n\n*** DISK DRIVER ***\n\n");
	printf("Creo un disco con 3 blocchi: test.disk");

	DiskDriver_init(&disk, "test.disk", 3);
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
	int ret = remove("test.disk");
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
	for(i = 0; i < 130; i++){
		sprintf(filename, "%d", i);
		fh = SimpleFS_createFile(dh, filename);
	}
	if(fh != NULL) SimpleFS_close(fh);

	// Li leggo
	printf("\n * Leggo la directory corrente (root) * \n");
	char** files = (char**) malloc(sizeof(char*) * dh->dcb->num_entries);
	ret = SimpleFS_readDir(files, dh);
	for (i=0; i<dh->dcb->num_entries; i++){
		printf("%s ", files[i]);
	}
	for (i=0; i<dh->dcb->num_entries; i++){
		free(files[i]);
	}
	free(files);
	printf("\n\n");
}
