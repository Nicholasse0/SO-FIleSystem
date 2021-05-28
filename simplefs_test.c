#include "simplefs.h"
#include "bitmap.c"
#include "disk_driver.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int agc, char** argv) {
  printf("FirstBlock size %ld\n", sizeof(FirstFileBlock));
  printf("DataBlock size %ld\n", sizeof(FileBlock));
  printf("FirstDirectoryBlock size %ld\n", sizeof(FirstDirectoryBlock));
  printf("DirectoryBlock size %ld\n", sizeof(DirectoryBlock));


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
	printf(" * Rilascio *\n\n");

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
}
