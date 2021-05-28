#include "simplefs.h"
#include "bitmap.c"
#include "disk_driver.c"

#include <stdio.h>
#include <stdlib.h>

int findBit(unsigned char b, int n) {
    char mask2[] = {128, 64, 32, 16, 8, 4, 2, 1};
    return ((b & mask2[n]) != 0);
}

void printBitMap(BitMap* bmp, int a){
	int i, bit;
	for(i = 0; i < 8; i++){
		bit = findBit(bmp->entries[a], i);
		printf("%d", bit);
	}
}

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
	printBitMap(bmp, 0);
	printf("\nEntries[1] = ");
	printBitMap(bmp, 1);
	printf("\nEntries[2] = ");
	printBitMap(bmp, 2);
	printf("\n\nPrendo il primo bit a 0, a partire dalla posizione 7\n");
	printf("Risultato = %d [CORRETTO = 7]\n", BitMap_get(bmp, 7, 0));

	printf("\nSetto questo bit a 1\n");
	BitMap_set(bmp, 4, 1);
	printf("Risultato = ");
	printBitMap(bmp, 0);
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
}
