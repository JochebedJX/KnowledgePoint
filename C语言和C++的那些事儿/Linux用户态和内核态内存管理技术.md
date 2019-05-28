# Linux用户态和内核态内存管理技术

------

 

   * [Linux用户态和内核态内存管理技术](#linux用户态和内核态内存管理技术)
         * [一、虚拟地址和物理地址之间的映射关系](#一虚拟地址和物理地址之间的映射关系)
         * [二、进程在用户空间和内核空间的切换问题](#二进程在用户空间和内核空间的切换问题)
         * [三、linux 用户态和内核态 slab内存分配器](#三linux-用户态和内核态-slab内存分配器)
            * [1、系统调用](#1系统调用)
            * [2、发生异常](#2发生异常)
            * [3、外围IO设备的中断；](#3外围io设备的中断)
         * [四、进程的地址空间](#四进程的地址空间)
         * [五、Linux内存管理 —— 内核态和用户态的内存分配方式](#五linux内存管理--内核态和用户态的内存分配方式)
            * [1、使用buddy系统管理ZONE](#1使用buddy系统管理zone)

      * [2. 用户态申请内存时的”lazy allocation”](#2-用户态申请内存时的lazy-allocation)

      * [3. OOM(Out Of Memory)](#3-oomout-of-memory)

      * [补充：如何满足DMA的连续内存需求](#补充如何满足dma的连续内存需求)
         * [六、<a href="https://blog.csdn.net/szu_tanglanting/article/details/16339809" rel="nofollow">linux内核 -内存管理模块概图</a>](#六linux内核--内存管理模块概图)
            * [1、从进程(task)的角度来看内存管理](#1从进程task的角度来看内存管理)
            * [2、从物理页面(page)的角度来看待内存管理](#2从物理页面page的角度来看待内存管理)

      * [3.task里面的vma和page怎么关联呢？](#3task里面的vma和page怎么关联呢)

         

​	通常程序访问的地址都是虚拟地址，用32位操作系统来讲，访问的地址空间为4G，linux将4G分为两部分。如图1所示，其中0~3G为用户空间，3~4G为内核空间。通过MMU这两部分空间都可以访问到实际的物理内存。

进程在用户态只能访问0~3G，只有进入内核态才能访问3G~4G  ：

- 进程通过系统调用进入内核态

- 每个进程虚拟空间的3G~4G部分是相同的  
- 进程从用户态进入内核态不会引起CR3的改变但会引起堆栈的改变

 

图1![img](https://img-my.csdn.net/uploads/201412/10/1418199587_2134.jpg) 

 

 ![img](https://img-my.csdn.net/uploads/201412/10/1418199588_1395.jpg) 

### 一、虚拟地址和物理地址之间的映射关系

​	页作为基本的映射单元，一页的大小一般为4K，用户空间不是特别复杂，但内核空间又将1G的虚拟空间按区划分为3个部分：**ZONE_DMA（**内存开始的16MB**）** 、**ZONE_NORMAL（**16MB~896MB**）**、**ZONE_HIGHMEM（**896MB ~ 结束**）**。

​	为什么要有高端内存的概念呢?

​	如逻辑地址0xc0000003对应的物理地址为0×3，0xc0000004对应的物理地址为0×4，… …，逻辑地址与物理地址对应的关系为

物理地址 =逻辑地址 – 0xC0000000

| **逻辑地址**   | **物理内存地址**  |
| -------------- | ----------------- |
| 0xc0000000     | 0×0               |
| 0xc0000001     | 0×1               |
| 0xc0000002     | 0×2               |
| 0xc0000003     | 0×3               |
| …              | …                 |
| 0xe0000000     | 0×20000000        |
| …              | …                 |
| **0xffffffff** | **0×40000000 ??** |

​	假设按照上述简单的地址映射关系，那么内核逻辑地址空间访问为0xc0000000 ~ 0xffffffff，那么对应的物理内存范围就为0×0 ~ 0×40000000，即只能访问1G物理内存。若机器中安装8G物理内存，那么内核就只能访问前1G物理内存，后面7G物理内存将会无法访问，因为内核的地址空间已经全部映射到物理内存地址范围0×0 ~ 0×40000000。即使安装了8G物理内存，那么物理地址为0×40000001的内存，内核该怎么去访问呢？代码中必须要有内存逻辑地址的，0xc0000000 ~ 0xffffffff的地址空间已经被用完了，所以无法访问物理地址0×40000000以后的内存

![img](https://img-my.csdn.net/uploads/201412/10/1418199588_7173.jpg)

​            图2

![img](https://img-my.csdn.net/uploads/201412/10/1418199588_9675.jpg)

​              图3

​	前面我们解释了高端内存的由来。 Linux将内核地址空间划分为三部分ZONE_DMA、ZONE_NORMAL和ZONE_HIGHMEM，高端内存HIGH_MEM地址空间范围为 0xF8000000 ~ 0xFFFFFFFF（896MB～1024MB）。那么如内核是**如何借助128MB高端内存地址空间是如何实现访问可以所有物理内存**？

​	当内核想访问高于896MB物理地址内存时，从0xF8000000 ~ 0xFFFFFFFF地址空间范围内找一段相应大小空闲的逻辑地址空间，借用一会。借用这段逻辑地址空间，建立映射到想访问的那段物理内存（即填充内核PTE页面表），**临时用一会，用完后归还**。这样别人也可以借用这段地址空间访问其他物理内存，实现了使用有限的地址空间，访问所有所有物理内存。如图3。

### 二、进程在用户空间和内核空间的切换问题

​	内核在创建进程的时候，在创建task_struct的同事，会为进程创建相应的堆栈。每个进程会有两个栈，一个用户栈，存在于用户空间，一个内核栈，存在于内核空间。当进程在用户空间运行时，cpu堆栈指针寄存器里面的内容是用户堆栈地址，使用用户栈；当进程在内核空间时，cpu堆栈指针寄存器里面的内容是内核栈空间地址，使用内核栈。

​	当进程因为中断或者系统调用而陷入内核态之行时，进程所使用的堆栈也要从用户栈转到内核栈。

​	进程陷入内核态后，先把用户态堆栈的地址保存在内核栈之中，然后设置堆栈指针寄存器的内容为内核栈的地址，这样就完成了用户栈向内核栈的转换；当进程从内核态恢复到用户态之行时，在内核态之行的最后将保存在内核栈里面的用户栈的地址恢复到堆栈指针寄存器即可。这样就实现了内核栈和用户栈的互转。

​	那么，我们知道从内核转到用户态时用户栈的地址是在陷入内核的时候保存在内核栈里面的，但是在陷入内核的时候，我们是如何知道内核栈的地址的呢？

​	关键在进程从用户态转到内核态的时候，进程的内核栈总是空的。这是因为，当进程在用户态运行时，使用的是用户栈，当进程陷入到内核态时，内核栈保存进程在内核态运行的相关信息，但是一旦进程返回到用户态后，内核栈中保存的信息无效，会全部恢复，因此每次进程从用户态陷入内核的时候得到的内核栈都是空的。所以在进程陷入内核的时候，直接把内核栈的栈顶地址给堆栈指针寄存器就可以了。

### 三、linux 用户态和内核态 slab内存分配器

​	首先要说明一个特权级的概念 ：为什么会有特权级？因为在程序中如fork,malloc这些函数其实是操作系统提供的系统调用，它是要调用底层的，如分配内存，拷贝父进程相关信息，拷贝页表项等等；那么这些不可能是一个普通用户程序能够有权限去调用的，所以这些是属于内核去配置和执行的，所以就有了特权级：一般inte X86有3个级别，如：0-3，3级别最低的，它只有最基本的权利。IO：epoll poll select这些都要系统调用核心态去实现；

​	一般程序都是处于用户态的，什么时候转内核态呢？

#### 1、系统调用  

​	这是用户态进程主动要求切换到内核态的一种方式，用户态进程通过系统调用申请使用操作系统提供的服务程序完成工作，比如前例中fork()实际上就是执行了一个创建新进程的系统调用。而系统调用的机制其核心还是使用了操作系统为用户特别开放的一个中断来实现，例如Linux的int 80h中断。

#### 2、发生异常 

​	当CPU在执行运行在用户态下的程序时，发生了某些事先不可知的异常，这时会触发由当前运行进程切换到处理此异常的内核相关程序中，也就转到了内核态，比如缺页异常。

#### 3、外围IO设备的中断；

​	当外围设备完成用户请求的操作后，会向CPU发出相应的中断信号，这时CPU会暂停执行下一条即将要执行的指令转而去执行与中断信号对应的处理程序，如果先前执行的指令是用户态下的程序，那么这个转换的过程自然也就发生了由用户态到内核态的切换。比如硬盘读写操作完成，系统会切换到硬盘读写的中断处理程序中执行后续操作等。

​	他们最终都是通过中断来实现的，系统调用时主动，异常，IO是被动。

### 四、进程的地址空间

​	linux采用虚拟内存管理技术，每一个进程都有一个3G大小的独立的进程地址空间，这个地址空间就是用户空间。每个进程的用户空间都是完全独立、互补相干的。进程访问内核空间的方式：**系统调用和中断**。 
	创建进程等进程相关操作都需要分配内存给进程。这时进程申请和获得的不是物理地址，仅仅是虚拟地址。 
实际的物理内存只有当进程真的去访问新获取的虚拟地址时，才会由“请页机制**”产生“缺页”异常**，从而进入分配实际叶框的程序。该异常是虚拟内存机制赖以存在的基本保证---它会告诉内核去为进程分配物理页，并建立对应的页表，这之后虚拟地址才实实在在的映射到了物理地址上。

![img](https://img-blog.csdn.net/20131115145840703?watermark/2/text/aHR0cDovL2Jsb2cuY3Nkbi5uZXQvdGFuZ2xhbnRpbmcxMg==/font/5a6L5L2T/fontsize/400/fill/I0JBQkFCMA==/dissolve/70/gravity/SouthEast)



- 这时进程申请和获得的不是物理地址，仅仅是虚拟地址。 下面的图可以说明：地址的虚拟地址空间

  进程的形态：![img](https://img-blog.csdn.net/20131115150120390?watermark/2/text/aHR0cDovL2Jsb2cuY3Nkbi5uZXQvdGFuZ2xhbnRpbmcxMg==/font/5a6L5L2T/fontsize/400/fill/I0JBQkFCMA==/dissolve/70/gravity/SouthEast)

- 创建一组vm_area_struct

- 圈定一个虚拟用户空间，将其起始结束地址(elf段中已设置好)保存到vm_start和vm_end中。

- 将[磁盘](http://www.chinabyte.com/keyword/%E7%A3%81%E7%9B%98/)file句柄保存在vm_file中

- 将对应段在磁盘file中的偏移值(elf段中已设置好)保存在vm_pgoff中;
- 将操作该磁盘file的磁盘操作函数保存在vm_ops中

![img](https://img-blog.csdn.net/20131115150441218?watermark/2/text/aHR0cDovL2Jsb2cuY3Nkbi5uZXQvdGFuZ2xhbnRpbmcxMg==/font/5a6L5L2T/fontsize/400/fill/I0JBQkFCMA==/dissolve/70/gravity/SouthEast)

2. 缺页异常通过进程页表进行空间请求！

- 如果是堆空间申请比较大的空间就直接利用伙伴系统来配置！

- slab分配器是Linux内存管理中非常重要和复杂的一部分，其工作是针对一些经常分配并释放的对象，如进程描述符等，这些对象的大小一般比较小，如果直接采用伙伴系统来进行分配和释放，不仅会造成大量的内碎片，而且处理速度也太慢。slab是一种内核利用缓存和位对齐的原理产生缓存链接列表，slab分配器是基于对象进行管理的，相同类型的对象归为一类(如进程描述符就是一类)，每当要申请这样一个对象，slab分配器就从一个slab列表中分配一个这样大小的单元出去，而当要释放时，将其重新保存在该列表中，而不是直接返回给伙伴系统。slab分配对象时，会使用最近释放的对象内存块，因此其驻留在CPU高速缓存的概率较高。

slab结构：（slab具有缓存池的概念这样可以提高分配速度和空间的控制）

具体可以查看https://www.ibm.com/developerworks/cn/linux/l-linux-slab-allocator/

http://blog.csdn.net/vanbreaker/article/details/7664296

![img](https://img-blog.csdn.net/20131115152127281?watermark/2/text/aHR0cDovL2Jsb2cuY3Nkbi5uZXQvdGFuZ2xhbnRpbmcxMg==/font/5a6L5L2T/fontsize/400/fill/I0JBQkFCMA==/dissolve/70/gravity/SouthEast)

3. 尽管仅仅在某些情况下才需要物理上连续的内存块，但是，很多内核代码都调用kmalloc()，而不是用vmalloc()获得内存。这主要是出于性能的考虑。vmalloc()函数为了把物理上不连续的页面转换为虚拟地址空间上连续的页，必须专门建立页表项。还有，通过 vmalloc()获得的页必须一个一个的进行映射（因为它们物理上不是连续的），这就会导致比直接内存映射大得多的缓冲区刷新。因为这些原因，vmalloc()仅在绝对必要时才会使用——典型的就是为了获得大块内存时，例如，当模块被动态插入到内核中时，就把模块装载到由vmalloc()分配的内存上。

> 通过[[落尘纷扰](https://blog.csdn.net/jasonchen_gbd) ](https://blog.csdn.net/jasonchen_gbd/article/details/79461385)的博客我们来看看内核态和用户态的内存分配方式

### 五、Linux内存管理 —— 内核态和用户态的内存分配方式

#### 1、使用buddy系统管理ZONE

我的这两篇文章[buddy系统](http://blog.csdn.net/jasonchen_gbd/article/details/44023801)和[slab分配器](http://blog.csdn.net/jasonchen_gbd/article/details/44024009)已经分析过buddy和slab的原理和源码，因此一些细节不再赘述。

所有zone都是通过buddy系统管理的，buddy system由Harry Markowitz在1963年提出。buddy的工作方式我就不说了，简单来说buddy就是用来管理内存的使用情况：一个页被申请了，别人就不能申请了。通过/proc/buddyinfo可以查看buddy的内存余量。

由于buddy是zone里面的一个成员，所以每个zone都有自己的buddy系统来管理自己的内存（因此，buddy管理的也是物理内存哦）。

```c++
[jchen@ubuntu]my_code:$ cat /proc/buddyinfo 
Node 0, zone      DMA      9     15      1      1      4      1      1      2      1      1      0 
Node 0, zone   Normal    157    902    332     76     77     77     47     23     11      1      0 
Node 0, zone  HighMem      1      2     13      3      3      2      4      0      0      0      0 1234
```

buddy的问题就是容易碎掉，即没有大块连续内存。对应用程序和内核非线性映射没有影响，因为有MMU和页表，但DMA不行，DMA engine里面没有MMU，一致性映射后必须是连续内存。

可以通过alloc_pages(gfp_mask, order)从buddy里面申请内存，申请内存大小都是2^order个页的大小，这样显然是不满足实际需求的。因此，基于buddy，**slab**(或slub/slob)对内存进行了二次管理，使系统可以申请小块内存。

Slab先从buddy拿到数个页的内存，然后切成固定的小块（称为object），再分配出去。从/proc/slabinfo中可以看到系统内有很多slab，每个slab管理着数个页的内存，它们可分为两种：一个是**各模块专用的**，一种是**通用的**。

在内核中常用的kmalloc就是通过slab拿的内存，它向通用的slab里申请内存。我们也就知道，kmalloc只能分配一个对象的大小，比如你想分配40B，实际上是分配了64B。在include/linux/kmalloc_sizes.h可以看到通用cache的大小都有哪些：

```c++
#if (PAGE_SIZE == 4096)
    CACHE(32)
#endif
    CACHE(64)
#if L1_CACHE_BYTES < 64
    CACHE(96)
#endif
    CACHE(128)
#if L1_CACHE_BYTES < 128
    CACHE(192)
#endif
    CACHE(256)
    CACHE(512)
    CACHE(1024)
    CACHE(2048)
    CACHE(4096)
    CACHE(8192)
    CACHE(16384)
    CACHE(32768)
    CACHE(65536)
    CACHE(131072)
#if KMALLOC_MAX_SIZE >= 262144
    CACHE(262144)
#endif
#if KMALLOC_MAX_SIZE >= 524288
    CACHE(524288)
#endif
#if KMALLOC_MAX_SIZE >= 1048576
    CACHE(1048576)
#endif
#if KMALLOC_MAX_SIZE >= 2097152
    CACHE(2097152)
#endif
#if KMALLOC_MAX_SIZE >= 4194304
    CACHE(4194304)
#endif
#if KMALLOC_MAX_SIZE >= 8388608
    CACHE(8388608)
#endif
#if KMALLOC_MAX_SIZE >= 16777216
    CACHE(16777216)
#endif
#if KMALLOC_MAX_SIZE >= 33554432
    CACHE(33554432)
#endif123456789101112131415161718192021222324252627282930313233343536373839404142434445
```

上述两种slab缓存，专用slab主要用于内核各模块的一些数据结构，这些内存是模块启动时就通过**kmem_cache_alloc**分配好占为己有，一些模块自己单独申请一块kmem_cache可以确保有可用内存。而各阶的通用slab则用于给内核中的kmalloc等函数分配内存。

要注意在slab分配器里面的“cache”特指struct kmem_cache结构的实例，与CPU的cache无关。在/proc/slabinfo中可以查看当前系统中已经存在的“cache”列表。

通过slabtop命令可以查看当前系统中slab内存的消耗情况，和top命令类似，是按照已分配出去的内存多少的顺序打印的：

```c++
[root@ubuntu]my_code:$  slabtop --once
 Active / Total Objects (% used)    : 475395 / 494078 (96.2%)
 Active / Total Slabs (% used)      : 13040 / 13040 (100.0%)
 Active / Total Caches (% used)     : 71 / 100 (71.0%)
 Active / Total Size (% used)       : 127416.02K / 129693.49K (98.2%)
 Minimum / Average / Maximum Object : 0.01K / 0.26K / 8.00K

  OBJS ACTIVE  USE OBJ SIZE  SLABS OBJ/SLAB CACHE SIZE NAME                   
145912 145912 100%    0.61K   5612       26     89792K ext4_inode_cache       
101696 100019  98%    0.12K   3178       32     12712K dentry                 
 41856  37012  88%    0.03K    327      128      1308K ext4_extent_status     
 35843  30265  84%    0.05K    491       73      1964K buffer_head            
 35328  35291  99%    0.06K    552       64      2208K kmalloc-64             
 21966  21478  97%    0.09K    523       42      2092K kmalloc-96             
 20736  19528  94%    0.03K    162      128       648K kmalloc-32             
 13056  13056 100%    0.02K     51      256       204K kmalloc-16             
 12116   9196  75%    0.30K    466       26      3728K radix_tree_node        
  9702   8814  90%    0.19K    462       21      1848K kmalloc-192            
  9472   9472 100%    0.03K     74      128       296K anon_vma               
  8192   8192 100%    0.01K     16      512        64K kmalloc-8              
  7061   6404  90%    0.34K    307       23      2456K inode_cache            
  5610   5610 100%    0.05K     66       85       264K Acpi-State             
  4250   4250 100%    0.02K     25      170       100K nsproxy                
  2048   2048 100%    0.06K     32       64       128K jbd2_journal_head123456789101112131415161718192021222324
```

因此，slab和buddy是上下级的调用关系，slab的内存来自buddy；它们都是内存分配器，只是buddy管理的是各ZONE映射区，slab管理的是buddy的各阶。

注意，vmalloc是直接向buddy要内存的，不经过slab，因此vmalloc申请内存的最小单位是一页。slab只从lowmem申请内存，因此拿到的内存在物理上是连续的，vmalloc可以从高端和低端拿内存。而用户态的malloc是通过brk/mmap系统调用每次向内核申请一页，然后在标准库里再做进一步管理供用户程序使用。 
![这里写图片描述](https://img-blog.csdn.net/20180308001933307?watermark/2/text/aHR0cDovL2Jsb2cuY3Nkbi5uZXQvamFzb25jaGVuX2diZA==/font/5a6L5L2T/fontsize/400/fill/I0JBQkFCMA==/dissolve/70)

## 2. 用户态申请内存时的”lazy allocation”

用户态申请内存时，库函数并不会立即从内核里去拿，而是COW(copy on write)的，要不然用户态细细碎碎的申请释放都要跟内核打交道，那频繁系统调用的代价太大，并且内核每次都是一页一页的分配给用户态，小内存让内核很头疼的。 
但是库函数会欺骗用户程序，你申请10MB，就让你以为已经拥有了10MB内存，但是只有你真的要用的时候，C库才会一点一点的从内核申请，直到10MB都拿到，这就是用户态申请内存的**lazy模式**。有时用户程序为了立即拿到这10MB内存，在申请完之后，会立即把内存写一遍（例如memset为0），让C库将内存都真正申请到。Linux会欺骗用户程序，但不欺骗内核，内核中的kmalloc/vmalloc就真的是要一个字节内存就没了一个字节。

因此malloc在刚申请（brk或mmap）的时候，10MB所有页面在页表中全都映射到同一个零化页面（ZERO_PAGE，全局共享的页，页的内容总是0，用于zero-mapped memory areas等用途），内容全是0，且页表上标记这10MB是只读的，在写的时候发生page fault，才去一页一页的分配内存和修改页表。所以brk和mmap只是扩展了你的虚拟地址空间，而不是去拿内存。在你实际去写内存的时候，内核会先把这个只读0页面拷贝到给你新分配的页面，然后执行你的写操作。

由于上面的COW，用户申请了10MB，系统不会立即给你，但告诉你申请成功了。Linux内核的lazy模式可以减少不必要的内存浪费，因为用户态程序的行为不可控制，如果有一个程序申请100M内存又不用，就浪费了。如果一段时间后，系统内存被其他地方消耗，已经不足以给你10MB了，你这时慢慢通过COW使用内存的时候，C库就拿不到内存了。就会出现**OOM**。

补充一点，由于用户进程申请内存是zero页面拷贝的，因此用户态向kernel新申请的页面都是清0的，这样可以防止用户态窃取内核态的数据。但是如果用户程序用完释放了，但还没还给内核，这时相同的进程又申请到了这段内存，那内存里就是原来的数据，不清0的。而内核的kmalloc/vmalloc就没这个动作了，想要清0页面可以用kzalloc/vzalloc。

进程**栈**的内存分配也是lazy的，因为进程栈对应的VMA的vma->vm_flags带有VM_GROWSDOWN标记，这样，在page fault处理的时候kernel就知道落在了stack区域，就会通过expand_stack(vma, address)将栈扩展（vma区域的vm_start降低），这时如果扩展超过RLIMIT_STACK或RLIMIT_AS的限制，就会返回-ENOMEM。因此，对于进程栈，用户态不用做申请内存的动作，只需将sp下移即可，这是每个函数都要做的事情。

## 3. OOM(Out Of Memory)

OOM即是内存不够用了，在内核中会选择杀掉某个进程来释放内存，内核会给所有进程打分，分最高的则被杀掉，**打分的依据主要是看谁占的内存多**（当然是杀掉占内存的多的进程才能释放更多内存）。每个进程的/proc/pid/oom_score就是当前得分，OOM的时候就会选择分数最高的那个杀掉。被干掉之后，OOM的打印中也会打印出这个进程的score。 
**评分标准**（mm/oom_kill.c中的badness()给每个进程一个oom score）有：

1. 根据resident内存、page table和swap的使用情况，采用百分比乘以10，因此最高1000分，最低0分。
2. root用户进程减30分。
3. oom_score_adj: oom_score会加上这个值。可以在/proc/pid/oom_score_adj中修改（可以是负数），这样来人为地调整score结果。
4. oom_adj: -16~15的系数调整。修改/proc/pid/oom_adj里面的值，是一个系数，因此会在原score上乘上系数。数值越大，score结果就会变得越大，数值为负数时，score就会变得比原值小。

修改了3、4之后，/proc/pid/oom_score中的值就会随之改变。这样的话，就可以人为地干预OOM杀掉的进程。

注意，任何一个zone内存不足，都会触发OOM。

Android就利用了修改评分标准的特点，对于转向后台的进程打分提高，对前台进程的打分降低一点，尽可能防止前台进程退出。而进程进入后台时，Android并不杀死它，而是让他活着，如果这时系统内存足够，那么后台进程就一直活着，下次再调出这个进程时就很快。而如果某时刻内存不够了，那个OOM就会根据评分优先杀掉后台进程，让前台进程活着，而后台进程的重启只是稍微影响用户体验而已。

## 补充：如何满足DMA的连续内存需求

我们说buddy容易碎，但DMA通常只能操作一段物理上连续的内存，因此我们应该保证系统有足量的连续内存以使DMA正常工作。 
**1.** 预分配一块内存 
可以在系统启动时就预留出部分内存给DMA专用，这通常要在bootmem的阶段做，使这部分内存和buddy系统分离。并且需要提供申请释放内存的API给每个有需求的device。可以借用bigphysarea来完成。这种做法的缺点是这块连续内存永远不能给其他地方用（即使有没有被使用），可能被浪费，并且需要额外的物理内存管理。但这种预分配的思想在很多场合是最省力也很常用的。 
**2.** IOMMU 
如果device的DMA支持IOMMU(MMU for I/O)，也就是DMA内部有自己的MMU，就相当于MMU之于CPU（将virtual address映射到physical address），IOMMU可以将device address映射到physical address。这样就不再需要物理地址连续了。IOMMU相比普通DMA访存要耗时且耗电，不太常见。 
**3.** CMA 
处理DMA中的碎片有一个利器，CMA（连续内存分配器，在kernel v3.5-rc1正式被引入）。和磁盘碎片整理类似，这个技术也是将内存碎片整理，整合成连续的内存。因为对一个虚拟地址，它可以在不同时间映射到不同的物理地址，只要内容不变就行，对程序员是透明的。

上面讲了，物理地址映射关系对于CPU来讲是透明的，因此可以说虚拟内存是**可移动（movable）**的，但内核的内存一般不移动，应用程序一般就可以。应用程序在申请内存的时候可以标记我的这块内存是__GFP_MOVABLE的，让CMA认为可搬移。 
CMA的原理就是标记一段连续内存，这段内存平时可以作为movable的页面使用。那么应用程序在申请内存时如果打上movable的标记，就可以从这段连续内存里申请。当然，这段内存慢慢就碎了。当一个设备的DMA需要连续内存的时候，CMA就可以发挥作用了：比如设备想申请16MB连续内存，CMA就会从其他内存区域申请16MB，这16MB可能是碎的，然后将自己区域中已经被分出去的16MB的页面一一搬移到新申请的16MB页面中，这时CMA原来标记的内存就空出来了。CMA还要做一件事情，就是去修改被搬离的页所属的哪些进程的页表，这样才能让用户程序在毫无知觉的情况下继续正常运行。

注意，CMA的API是封装到DMA里面，所以你不能直接调用CMA接口，DMA的底层才用CMA（当然DMA也可以不用CMA机制，如果你的CPU不带CMA就更不用说了）。如果你的系统支持CMA，dma_alloc_coherence()内部就可能用CMA实现。 
dts里面可指定哪部分内存是可CMA的。可以是全局的CMA pool，也可以为某个特定设备指定CMA pool。具体填法见内核源码中的 Documentation\devicetree\bindings\reserved-memory\reserved-memory.txt。



### 六、[linux内核 -内存管理模块概图](https://blog.csdn.net/szu_tanglanting/article/details/16339809)

#### 1、从进程(task)的角度来看内存管理

![这里写图片描述](https://img-blog.csdn.net/20180715125844375?watermark/2/text/aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L2h6Z2RpeWVy/font/5a6L5L2T/fontsize/400/fill/I0JBQkFCMA==/dissolve/70)

1. 每个进程对应一个task_struct;
2. 每个task_struct 里面包含指向mm_struct 的指针mm, 
   mm_struct 里面的主要成员： 
   a. 指向vma链表的头指针：mmap 
   b. 指向vma红黑树的根节点: mm_rb 
   c. 指向进程列表的指针pgb;
3. vma(vm_area_struct): 进程虚存管理的最基本的管理单元应该是struct vm_area_struct了，它描述的是一段连续的、具有相同访问属性的虚存空间，该虚存空间的大小为物理内存页面的整数倍。通常，进程所使用到的虚存空间不连续，且各部分虚存空间的访问属性也可能不同。所以一个进程的虚存空间需要多个vm_area_struct结构来描述。 
   结构体的主要成员： 
   a. vma的起始和结束地址； 
   b. 指向vma 前后节点的指针 
   c. 指向当前vma在红黑树中的位置指针； 
   d. 指向当前vma所归属的mm_struct 的指针；

#### 2、从物理页面(page)的角度来看待内存管理

![这里写图片描述](https://img-blog.csdn.net/20180715130011659?watermark/2/text/aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L2h6Z2RpeWVy/font/5a6L5L2T/fontsize/400/fill/I0JBQkFCMA==/dissolve/70)

1. mem_map[] 里面包含了所有的物理页面，可以通过索引来访问。
2. 每一个物理页面用 struct page来表示，page 里面的主要成员介绍： 
   a. flags里面包含了当前页面的标志 
   另外也包含了其所属的zone的标志。 
   b. mapping：表示这个页面指向的地址空间。反响映射(reverse mapping)时使用,比如页面回收时。 
   c. _mapcount：表示这个页面被进程映射的个数。 
   d. _count：内核中引用该页面的次数,当其为0时，表示这个页面空闲。
3. struct zone： 
   zone 里面的主要成员介绍： 
   a. watermark[]: zone的3个水位值：min/low/high, 在kswapd页面回收中会用到； 
   b. lowmem_reserve[]: zone中遗留的内存: 
   <https://blog.csdn.net/kickxxx/article/details/883573> 
   kernel在分配内存时，可能会涉及到多个zone，分配会尝试从zonelist第一个zone分配，如果失败就会尝试下一个低级的zone（这里的低级仅仅指zone内存的位置，实际上低地址zone是更稀缺的资源）。我们可以想像应用进程通过内存映射申请Highmem 并且加mlock分配，如果此时Highmem zone无法满足分配，则会尝试从Normal进行分配。这就有一个问题，来自Highmem的请求可能会耗尽Normal zone的内存，而且由于mlock又无法回收，最终的结果就是Normal zone无内存提供给kernel的正常分配，而Highmem有大把的可回收内存无法有效利用。 
   因此针对这个case，使得Normal zone在碰到来自Highmem的分配请求时，可以通过lowmem_reserve声明：可以使用我的内存，但是必须要保留lowmem_reserve[NORMAL]给我自己使用。 
   同样当从Normal失败后，会尝试从zonelist中的DMA申请分配，通过lowmem_reserve[DMA]，限制来自HIGHMEM和Normal的分配请求。 
   c. zone_pgdat: 指向内存节点 
   在UMA系统上，只使用一个NUMA结点来管理整个系统内存 
   d. lruvec: LRU的链表集合，用于内存页面回收(page reclaim) 
   共5个链表： 
   匿名页面的不活跃链表、匿名页面的活跃链表 
   文件页面的不活跃链表、文件页面的活跃链表 
   不可回收页面链表

```c++
struct lruvec {
    struct list_head lists[NR_LRU_LISTS];
    struct zone_reclaim_stat reclaim_stat;
#ifdef CONFIG_MEMCG
    struct zone *zone;
#endif
};
enum lru_list {
    LRU_INACTIVE_ANON = LRU_BASE,
    LRU_ACTIVE_ANON = LRU_BASE + LRU_ACTIVE,
    LRU_INACTIVE_FILE = LRU_BASE + LRU_FILE,
    LRU_ACTIVE_FILE = LRU_BASE + LRU_FILE + LRU_ACTIVE,
    LRU_UNEVICTABLE,
    NR_LRU_LISTS
};
123456789101112131415
```

## 3.task里面的vma和page怎么关联呢？

ARM32的页表：

![è¿éåå¾çæè¿°](https://img-blog.csdn.net/20180715174204622?watermark/2/text/aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L2h6Z2RpeWVy/font/5a6L5L2T/fontsize/400/fill/I0JBQkFCMA==/dissolve/70) 

​	页表就是用于将虚拟地址转换为物理地址的转换关系表。访问虚拟地址时，计算机通过页表找到对应的实际物理地址访问。 

![è¿éåå¾çæè¿°](https://img-blog.csdn.net/20180715174633861?watermark/2/text/aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L2h6Z2RpeWVy/font/5a6L5L2T/fontsize/400/fill/I0JBQkFCMA==/dissolve/70) 

​	怎么完成从pgd 到 page的转化呢？ 
	linux 内核code是通过follow_page来完成的，原型如下：

```c++
static inline struct page *follow_page(struct vm_area_struct *vma,unsigned long address, unsigned int foll_flags)
```

主要分成2步： 
1. 由虚拟地址vaddr通过查询页表找到pte; 
2. 由pte找出页帧号pfn,然后在mem_map[]中找到相应的struct page结构。

这2步可以细化为如下几步： 
a. vma得到其所属的mm; 
b. mm->pgb(进程页表pgb的起始位置) 
c. mm->pgb 和 address 得到 address对应的pgd

```c++
#define PGDIR_SHIFT     21
/* to find an entry in a page-table-directory */
#define pgd_index(addr)     ((addr) >> PGDIR_SHIFT)

#define pgd_offset(mm, addr)    ((mm)->pgd + pgd_index(addr))
```

d. pgd得到pte 
在ARM页表中，无pud和pmd，如下代码中的pmd就是步骤c中得到的pgd.

```c++
ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
#define pte_offset_map_lock(mm, pmd, address, ptlp) \
({                          \
    spinlock_t *__ptl = pte_lockptr(mm, pmd);   \
    pte_t *__pte = pte_offset_map(pmd, address);    \
    *(ptlp) = __ptl;                \
    spin_lock(__ptl);               \
    __pte;                      \
})
#define pte_offset_map(pmd,addr)    (__pte_map(pmd) + pte_index(addr))
```

e. pte得到pfn

```c++
unsigned long pfn = pte_pfn(pte);
#define pte_pfn(pte)        ((pte_val(pte) & PHYS_MASK) >> PAGE_SHIFT)
```

f. pfn得到page

```c++
page = vm_normal_page(vma, address, pte);
```

