#include "bitmap.h"

#include <stdio.h>

#define mask 0x01 // 00000001

int BitMap_getBit(BitMap* bmp, int pos){
	if(pos >= bmp->num_bits) return -1;
	BitMapEntryKey map = BitMap_blockToIndex(pos);
	return bmp->entries[map.entry_num] >> map.bit_num & mask;
}

BitMapEntryKey BitMap_blockToIndex(int num){
	BitMapEntryKey map;
	int byte = num / 8;
	map.entry_num = byte;
	char offset = num - (byte * 8);
	map.bit_num = offset;
	return map;
}

int BitMap_indexToBlock(int entry, uint8_t bit_num){
	if (entry < 0 || bit_num < 0) return -1;
	return (entry * 8) + bit_num;
}

int BitMap_get(BitMap* bmp, int start, int status){
	if(start > bmp->num_bits) return -1;
	while(start < bmp->num_bits){
		if(BitMap_getBit(bmp, start) == status) return start;
		start++;
	}
	return -1;
}

int BitMap_set(BitMap* bmp, int pos, int status){
	if(pos >= bmp->num_bits) return -1;
	
	BitMapEntryKey map = BitMap_blockToIndex(pos);
	unsigned char num_bit = 1 << map.bit_num;
	unsigned char entry_i = bmp->entries[map.entry_num];
	if(status == 1){
		// OR logico
		bmp->entries[map.entry_num] = entry_i | num_bit;
		return entry_i | num_bit;
	}
	else{
		// AND logico
		bmp->entries[map.entry_num] = entry_i & (~num_bit);
		return entry_i & (~num_bit);
	}
}

void BitMap_print(BitMap* bmp, int n){
	int i, bit;
	char mask2[] = {128, 64, 32, 16, 8, 4, 2, 1};
	for(i = 0; i < 8; i++){
		bit = ((bmp->entries[n] & mask2[i]) != 0);
		printf("%d", bit);
	}
}