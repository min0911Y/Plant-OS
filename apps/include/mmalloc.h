// min0911_'s memory alloc library, the project began on 2023/11/18
// 
// 
//  声明：此项目没有实用价值，瞎写的
// 
// 
// -------------------------------------------------------------------
// idea: thought on 2023 11.18
// 设计一个虚拟的池子，用insert函数往里面投掷内存，insert要注意判断是否与内存池中的内存有连续的，如果有连续的，则合并
// malloc 即遍历此内存池，找到满足条件的，然后删掉该内存，返回
// free即将这个内存重新insert回去
#pragma once
#include <stdint.h>
// Attention: *limit不可被访问
typedef enum {
	MMEMORY_NO_MORE_MEMORY, // 内存不够了
	MMEMORY_NO_MORE_FREE_BLOCK, // 内存碎片过多
	MMEMORY_NOTHING, // 没有错误
	MMEMORY_INVAILD_PARAMETER // 非法参数
} MMEMORY_ERROR;
// start和limit若都为0，则这个格子还没使用
typedef struct {
	uint32_t start;
	uint32_t limit; // 不可被访问/写入
} mmemory_free;
typedef struct {
	int free_block_num_max; // 最大内存碎片数
	int useable_memory_for_user_start; // 用户可用内存的开始（可访问/写入）
	int useable_memory_for_user_limit; // 用户可用内存的最高点（不可被访问/写入）
	MMEMORY_ERROR mmemory_err; // 错误
	mmemory_free* memory_pool; // 内存池
}mm_head; // 头，在堆的开头，记录一些信息


// ----------------------------------------
// macros
// ----------------------------------------

// 判断某个格子是否空闲
// --> start和limit都为0
//   --> start和limit 不可能为负数
//     --> start+limit若为0，则start=0，limit=0 
#define MMEMORY_IS_FREE(n) (n.start + n.limit == 0)
#define MMEMORY_IS_FREE_PTR(n) (n->start + n->limit == 0)

// 设置某个格子空闲
#define MMEMORY_SET_FREE(n) n.start = 0;n.limit = 0
#define MMEMORY_SET_FREE_PTR(n) n->start = 0;n->limit = 0

// 获取某个格子的大小
#define MMEMORY_GET_SIZE(n) (n.limit - n.start)
#define MMEMORY_GET_SIZE_PTR(n) (n->limit - n->start)

// ----------------------------------------
// functions
// ----------------------------------------
mm_head* mmalloc_init(uint32_t start, uint32_t limit, uint32_t free_block_number_max);
void mmemory_insert(mm_head* head, uint32_t start, uint32_t limit);
void* mmemory_alloc(mm_head* head, uint32_t size);
void mmemory_set_free(mm_head* head, void* ptr, uint32_t size);