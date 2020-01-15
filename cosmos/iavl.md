# 可认证数据结构IAVL+树

longcpp ( longcpp9@gmail.com ), JIa (yin.jia987@gmail.com)

20200115

基于账户模型的cosmos-sdk中需要可认证数据结构(Authenticated Data Structure, ADS)来存储包括账户的状态信息在内的各类信息. 以太坊中使用Merkle Patricia Tree (MPT树)数据结构来提供相应的功能, 而cosmos-sdk另辟蹊径通过组合Mekle树和自平衡的二叉搜索树构建了新型的ADS数据结构IAVL+树. 本文首先介绍IAVL+树结构的概念和实现, 然后分析这一数据结构在cosmos-sdk中的应用. IAVL+树的实现参照`github.com/tendermint/iavl`的[`v0.12.4`](https://github.com/tendermint/iavl/tree/v0.12.4)版本, cosmos-sdk的实现参考`github.com/cosmos/cosmos-sdk`的[`v0.37.5`](https://github.com/cosmos/cosmos-sdk/tree/v0.37.5)版本.

IAVL+是cosmos-sdk中各个模块所依赖的`KVStore`的底层实现, 全称为"Immutable AVL +"树. 其设计目标是为键值对(例如账户余额)提供可持久化存储的能力, 同时支持版本化以及快照功能. IAVL+树中的节点是不可修改的(Immutable)并且通过节点的哈希值来进行索引.如果节点不可修改, 如何更新的节点的状态以反映存储状态的变化? 在IAVL+树中修改某个节点时, 会先生成一个新的节点, 然后用该节点来替换目标节点. 这种更新方式配合在节点中保存的版本信息, 就同时实现了版本化和生成快照的功能, 也就支持了状态版本之间的快速切换. IAVL+树是基于AVL树构建而来的. 在AVL树中, 任意节点的左右子树的高度最多相差1. 当插入/删除做到导致某个节点的左右子树高度差值大于1时, 会出发自平衡操作. AVL树中通常叶子节点和中间节点都可以存储键值对, 而AVL+树通过修改AVL树使得仅有叶子节点存储键值对, 而中间节点仅用来存储键以及左右子树的信息. 这种改动可以简化数据结构的实现. IAVL+树继承了AVL树的特性: 自平衡的二叉搜索树, 对于n个叶子节点的查找/插入/删除操作的时间复杂度都为O(logn), 在新增或删除Node时可能会触发一次或者多次树的旋转操作以保证树的平衡.

## IAVL+树的节点与存储

`ival/node.go`中给出了节点的具体定义. 值得注意的是, 叶子节点和中间节点的数据结构相同, 差别在于节点中具体字段的值不同. 对于叶子节点来说,其中的`size`字段一直为1, `height`字段一直为0, 并且`value`字段真正存储了对应某个键的值, 而关于左右孩子的字段则为`nil`. 对于中间节点来说, `size`字段大于1, `height`字段大于0, `value`字段为空, 而`key`字段则等于其右子树中节点的`key`的最小值.由此可知, 在IAVL+树中叶子节点的`key`值是按照从左到右的顺序逐渐增大. 通过在中间节点存储右子树叶子节点`key`的最小值, 可以在根据`key`值进行查找时进行二分查找. 

```go
// Node represents a node in a Tree.
type Node struct {
	key       []byte // 节点的键
	value     []byte // 叶子节点的值, 如果是中间节点则为nil
	version   int64  // IAVL+树上首次插入该节点时的版本号
	height    int8   // 节点的高度. 叶子节点的高度为0
	size      int64  // 以当前节点代表的子树包含的叶子节点个数, 叶子节点该值为1
	hash      []byte // 上面字段以及leftHash和rightHash的哈希值
	leftHash  []byte // 左孩子的哈希值
	leftNode  *Node  // 左孩子的指针
	rightHash []byte // 右孩子的哈希值
	rightNode *Node  // 右孩子的指针
	persisted bool   // 标记当前节点是否已经持久化到数据库中
}
```

下图中展示了一个含有8个节点的二叉树, 其中大写字母用来指代具体的节点, 而每个节点中的数字表示该节点的键`key`字段.,为了简化起见,图中没有叶子节点的`value`字段.

![tree-8Leaves](/Users/long/Downloads/plantuml/tree-8Leaves.png)

虽然叶子节点和中间节点复用了相同的数据结构`Node`, 但是由于字段值的不同, 两种节点的哈希值计算过程也不相同:

- 计算叶子节点哈希值: `Hash(height||size||version||key||Hash(value))`
- 计算中间节点哈希值: `Hash(height||size||version||leftHash||rightHash)`

`Node`结构体中的`version`字段存储了该节点被首次插入树中时IAVL+树的版本号, 一个版本的IAVL+树就对应一个区块高度的状态集合. 如果一个`Node`在两个版本的IAVL+树中相同, 则后一版本的IAVL+树中可以直接应用前一版本的`Node`, 由此可以节省存储空间, `Node`信息的持久化通过在`iavl/nodedb.go`中定义的`nodeDB`结构体完成. 其中`db dbm.DB`字段代表一个持久化数据库, 其中`dbm.DB`是[`github.com/tendermint/tm-db`](https://github.com/tendermint/tm-db/blob/master/types.go)项目中定义的接口`DB`, `github.com/tendermint/tm-db`提供了通过[leveldb](https://github.com/tendermint/tm-db/blob/master/c_level_db.go), [rocksdb](https://github.com/tendermint/tm-db/blob/master/rocks_db.go)等数据库后端实现的数据库`DB`和`Batch`批处理接口. `nodeDB`另外有`Node`的缓存, 从`nodeDB`中读取`Node`时, 首先尝试从`nodeCache`中获取, 获取失败的话改为从底层数据库中获取.

```go
type nodeDB struct {
	mtx   sync.Mutex // Read/write lock. 
	db    dbm.DB     // Persistent node storage.
	batch dbm.Batch  // Batched writing buffer.

	latestVersion  int64
	nodeCache      map[string]*list.Element // Node cache.
	nodeCacheSize  int                      // Node cache size limit in elements.
	nodeCacheQueue *list.List               // LRU queue of cache elements. Used for deletion.
}
```

`Node`中的信息在序列化之后通过`db`持久化到数据库中. 序列化时通过Amino编码对相应的字段依次进行编码, 值得注意的是叶子节点和中间节点在存储时, 被序列化的字段不同:

- 叶子节点序列化: `Amino(height||size||version||key||value)` 
- 中间节点序列化: `Amino(height||size||version||key||leftHash||rightHash)`

## IAVL+树的读写与遍历

有了`Node`结构体和`nodeDB`, IAVL+树的定义在文件`iavl/immutable_tree.go`的结构体`ImmutableTree`中.  结构体比较简单, 只包括指向IAVL+树的根节点的指针`root`, 存储树中所有`Node`的数据库`ndb`以及这棵树的版本号. 如前所述, 每个区块执行完成之后都会形成一个新的IAVL+树来保存最新的状态集合, 而一次状态更新都会导致原先版本的IAVL+树中的一些节点被替换下来, 这些被替换下来的`Node`成为孤儿节点. 默认配置下, 所有的节点包括对应每个IAVL+树版本的根节点, 版本更新中形成的孤儿节点以及新生成的节点都会被`nodeDB`持久化到数据库中, 这就需要`nodeDB`的读写过程中能够区分三种类型的节点.

```go
type ImmutableTree struct {
	root    *Node // 指向根节点的指针
	ndb     *nodeDB // 存储节点的数据库
	version int64 // 该树的版本号
}
```

`github.com/tendermint/iavl`项目中为3类`Node`在存储到数据库中时, 定义了不同的键格式. 根节点, 其它节点及孤儿节点的键格式分别以字符`r`, `o`和`n`开始,并在后面级联不同的字段, 具体如下:

- 根节点的键格式: `r||version`
- 其它节点的键格式: `n||node.hash`
- 孤儿节点的键格式: `o||toVersion||fromVersion||node.hash`

其中根节点的键格式中包含了对应的IAVL+树的版本号, 其它节点的键格式中包含了节点的哈希值, 而孤儿节点的键格式比较特殊. 如前所述, 孤儿节点是在IAVL+树从老版本`fromVersion`更新到新版本`toVersion`时被替换下来的节点. 孤儿节点的这种键格式表明了该节点的生存期. 孤儿节点的键格式中, 以`toVersion||fromVersion`的顺序排列生存期对于快速删除孤儿节点有好处. 默认情况下`ndb`会存储所有的节点信息, 但`nodeDB`也可以根据用户自定义的剪枝选项`PruningOptions`对数据库中存储的内容进行精简, 例如从`nodeDB`中删除过版本为`v`的IAVL+树. 在这种场景下, 就可以根据`o||v`遍历所有目标孤儿节点,并执行删除操作, 后续再详细介绍剪枝选项.

`ImmutableTree`只能进行查询操作, 在涉及到树的修改时, 引入了结构体`MutableTree`, 定义在文件`iavl/mutable_tree.go`中. 其中`*ImmutableTree`表示当前的工作树, 也即新区块中的交易引发的状态变化就更新在这棵树上, 而`lastSaved`字段表示本次更新发生之前的IAVL+树, 也即上一次区块对应的状态树. 可以理解为根据`lastSaved`指向的之前的IAVL+树执行交易并更新到`*ImmutableTree`所代表的新的状态树, 被替换下来的孤儿节点存储在映射表`orphans`中. `versions`字段保存了当前数据库`ndb`中存储IAVL+树的版本号. 

```go
type MutableTree struct {
	*ImmutableTree                  // The current, working tree.
	lastSaved      *ImmutableTree   // The most recently saved tree.
	orphans        map[string]int64 // Nodes removed by changes to working tree.
	versions       map[int64]bool   // The previous, saved versions of the tree.
	ndb            *nodeDB
}
```

前面已经介绍过, IAVL+树的中间节点的`key`字段是其右子树叶子节点的`key`字段的最小值, 可以根据`key`值在IAVL+树中进行二分查找, 而其中的自平衡特性可以保证查找的复杂度为O(log(n)). 结合二叉树结构和递归函数, 对`ImmutableTree`和`MutableTree`的`Get`操作可以转换成为递归调用`Node`的方法: 例如定义在`iavl/Immutable_tree.go`中的`Get`方法: 

```go
func (t *ImmutableTree) Get(key []byte) (index int64, value []byte) {
	if t.root == nil {
		return 0, nil
	}
	return t.root.get(t, key) // Node的接口get方法
}
```

通过`t.root.get(t, key)`转换成为调用文件`iavl/node.go`中递归实现的`get`方法. 而`get`方法实现的逻辑很清晰, 根据`key`在树中递归进行二分搜索到达叶子节点. 然后判断叶子节点的键是否有`key`相等,并在成功找到时, 返回叶子节点的`index`和`value`.

```go
func (node *Node) get(t *ImmutableTree, key []byte) (index int64, value []byte) {
	if node.isLeaf() { // 递归终止条件 -> 到达叶子节点
		switch bytes.Compare(node.key, key) {
		case -1:
			return 1, nil 
		case 1:
			return 0, nil
		default:
			return 0, node.value // 有key对应的叶子节点, 返回index和value
		}
	}

	if bytes.Compare(key, node.key) < 0 { // 进入左子树
		return node.getLeftNode(t).get(t, key)
	}
	rightNode := node.getRightNode(t) // 进入右子树
	index, value = rightNode.get(t, key)
	index += node.size - rightNode.size // 从右子树返回, 累加index值
	return index, value // 叶子节点的index从0开始,从左到右按照1为步长递增
}
```

在树形结构进行操作时,同时希望能够遍历树中的某些节点,并且在遍历时对经过的每个节点进行处理. 这部分的逻辑实现在文件`iavl/node.go`文件中的`Node`方法`traverseInRange`中, 参见下面的代码. 该方法的输入参数包括当前IAVL+树的指针 `t`, 遍历的起始点`start`和结束点`end`,遍历的范围是键属于`[start, end)`中的中间节点和叶子节点, 是否要进行升序遍历的标记符`ascending`, 是否要访问结束点`end`的标记位`inclusive`,在树中的深度`depth` 以及对每个经过的节点的操作`cb func(*Node, uint8) bool`,其中函数`cb`有两个输入参数要处理的节点以及当前深度. 参照下面的实现代码, 可知当升序遍历时,相当于在`start`和`end`标记的范围内的节点进行前序遍历.

```go
func (node *Node) traverseInRange(t *ImmutableTree, start, end []byte, ascending bool, inclusive bool, depth uint8, cb func(*Node, uint8) bool) bool {
	afterStart := start == nil || bytes.Compare(start, node.key) < 0
	startOrAfter := start == nil || bytes.Compare(start, node.key) <= 0
	beforeEnd := end == nil || bytes.Compare(node.key, end) < 0
	if inclusive { // inclusive为true表示需要访问结束点end
		beforeEnd = end == nil || bytes.Compare(node.key, end) <= 0
	}

	// Run callback per inner/leaf node.
	stop := false
	if !node.isLeaf() || (startOrAfter && beforeEnd) { // 对中间节点和叶子节点都调用cb函数
		stop = cb(node, depth)
		if stop { // cb函数可以利用返回值控制是否继续遍历访问节点
			return stop
		}
	}
	if node.isLeaf() { // 递归终止条件
		return stop
	}

	if ascending { // 升序遍历
		// check lower nodes, then higher
		if afterStart { // 仍在遍历范围中, 前序遍历, 先左子树再右子树
			stop = node.getLeftNode(t).traverseInRange(t, start, end, ascending, inclusive, depth+1, cb)
		}
		if stop {
			return stop
		}
		if beforeEnd {
			stop = node.getRightNode(t).traverseInRange(t, start, end, ascending, inclusive, depth+1, cb)
		}
	} else { // 降序遍历
		// check the higher nodes first
		if beforeEnd { // 仍在遍历范围中, 前序遍历, 但是先访问右子树再访问左子树
			stop = node.getRightNode(t).traverseInRange(t, start, end, ascending, inclusive, depth+1, cb)
		}
		if stop {
			return stop
		}
		if afterStart {
			stop = node.getLeftNode(t).traverseInRange(t, start, end, ascending, inclusive, depth+1, cb)
		}
	}

	return stop
}
```

以前面8个叶子节点的二叉树图为例, 升序遍历键取值在范围`[2, 6]`的节点, 依次访问过(调用`cb`函数)的节点为 `A, B, E, J, K, C, F, L, M, G, N`, 参照下图,其中会执行`cb`函数的节点用不同的符号表示.

![treeTraverse26](/Users/long/Downloads/plantuml/treeTraverse26.png)

利用该函数可以对整棵IAVL+树进行遍历,如果不关心当前遍历的深度,则可以通过简单的包装忽略其中的`depth`参数, 参见`Node`方法`traverse`和`traverseWithDepth`的实现.

```go
func (node *Node) traverse(t *ImmutableTree, ascending bool, cb func(*Node) bool) bool {
	return node.traverseInRange(t, nil, nil, ascending, false, 0, func(node *Node, depth uint8) bool {
		return cb(node)
	})
}

func (node *Node) traverseWithDepth(t *ImmutableTree, ascending bool, cb func(*Node, uint8) bool) bool {
	return node.traverseInRange(t, nil, nil, ascending, false, 0, cb)
}
```

`ImmutableTree`没有`Set`和`Remove`方法, 对IAVL+树的更新操作由`MutableTree`完成. 文件`iavl/mutable_tree.go`中定义了这两个方法:

```go
func (tree *MutableTree) Set(key, value []byte) bool {
	orphaned, updated := tree.set(key, value) 
	tree.addOrphans(orphaned) // 保存此次操作导致的孤儿节点
	return updated
}

func (tree *MutableTree) Remove(key []byte) ([]byte, bool) {
	val, orphaned, removed := tree.remove(key)
	tree.addOrphans(orphaned)
	return val, removed
}
```

可以看到两个函数的函数体遵循相同的模式, 通过调用子方法完成相应的操作并将操作中产生的孤儿节点添加到`MutableTree`中的`orphans`映射表中. `tree.set`和`tree.remove`方法的实现也遵循相同的模式, 接下来关注 `tree.set`方法的实现, 参考下面代码中的注释.

```go
func (tree *MutableTree) set(key []byte, value []byte) (orphaned []*Node, updated bool) {
	if value == nil { // @value的值不允许为nil, 也即叶子节点的value不能为nil
		panic(fmt.Sprintf("Attempt to store nil value at key '%s'", key))
	}
	if tree.ImmutableTree.root == nil { // 若是空树则根据@key @value创建新节点作为根节点
		tree.ImmutableTree.root = NewNode(key, value, tree.version+1)
		return nil, false
	}
	tree.ImmutableTree.root, updated, orphaned = tree.recursiveSet(tree.ImmutableTree.root, key, value)
	// 通过recursiveSet完成实现set的逻辑
	return orphaned, updated // 返回孤儿节点和更新的节点
}

func (tree *MutableTree) recursiveSet(node *Node, key []byte, value []byte) (
	newSelf *Node, updated bool, orphaned []*Node,
) {
	version := tree.version + 1 // 每次Set会生成新版本的MutableTree

	if node.isLeaf() { // 递归终止条件->到达叶子节点
		switch bytes.Compare(key, node.key) { 
		case -1: // @key < node.key
			return &Node{ // 创建中间节点, 左右哈希值留到balance后再计算
				key:       node.key,// 中间节点的key为右叶子节点的key
				height:    1, 
				size:      2,
				leftNode:  NewNode(key, value, version), // 新叶子节点为左孩子
				rightNode: node, // 右孩子为当前叶子节点
				version:   version,
			}, false, []*Node{} // 没有产生新的孤儿节点
		case 1: // @key > node.key
			return &Node{ // 创建中间节点, 左右哈希值留到balance后再计算
				key:       key, // 中间节点的key为右叶子节点的key
				height:    1,
				size:      2,
				leftNode:  node, // 左孩子为当前叶子节点,
				rightNode: NewNode(key, value, version),// 新叶子节点为右孩子
				version:   version, 
			}, false, []*Node{} // 没有产生新的孤儿节点
		default: // 用@key和@value创建新节点替换当前节点, 当前节点成为孤儿节点
			return NewNode(key, value, version), true, []*Node{node}
		}
	} else { // 中间节点, 继续向着叶子节点前进
		orphaned = append(orphaned, node) // 途径的中间节点都变成孤儿节点
		node = node.clone(version) // 新版本树中的节点

		if bytes.Compare(key, node.key) < 0 { // @key < node.key 进入左子树
			var leftOrphaned []*Node
			node.leftNode, updated, leftOrphaned = tree.recursiveSet(node.getLeftNode(tree.ImmutableTree), key, value)
			node.leftHash = nil // leftHash is yet unknown
			orphaned = append(orphaned, leftOrphaned...) // 记录新的孤儿节点
		} else { // @key >= node.key 进入右子树, 中间节点key是右子树的最小值
			var rightOrphaned []*Node
			node.rightNode, updated, rightOrphaned = tree.recursiveSet(node.getRightNode(tree.ImmutableTree), key, value)
			node.rightHash = nil // rightHash is yet unknown
			orphaned = append(orphaned, rightOrphaned...) // 记录新的孤儿节点
		}

		if updated { // 仅更新了叶子节点的value字段,可直接返回,不影响height等字段
			return node, updated, orphaned 
		}
		node.calcHeightAndSize(tree.ImmutableTree) 
    // 再平衡并计算左右哈希值, 会产生新的孤儿节点
		newNode, balanceOrphaned := tree.balance(node) 
		return newNode, updated, append(orphaned, balanceOrphaned...)
	}
}
```

考察`MutableTree`的 `Remove `方法在文件`iavl/mutable_tree.go`中的实现, `Remove`方法接受一个`key`的参数并尝试从当前树中删除`key`对应的`value`. 成功删除返回被删除的值和`true`, 失败则返回`nil`和`false`. 与`Set`方法一样, `Remove` 的具体操作由函数`remove`和`recursiveRemove`完成, 参见下面展示的代码.

```go
func (tree *MutableTree) remove(key []byte) (value []byte, orphans []*Node, removed bool) {
	if tree.root == nil { // 空树无法删除任何@key对应的节点
		return nil, nil, false
	}
	newRootHash, newRoot, _, value, orphaned := tree.recursiveRemove(tree.root, key)
	if len(orphaned) == 0 { // 成功的删除操作肯定会产生新的孤儿节点
		return nil, nil, false
	}

	if newRoot == nil && newRootHash != nil {
		tree.root = tree.ndb.GetNode(newRootHash)
	} else {
		tree.root = newRoot
	}
	return value, orphaned, true
}

// 返回值说明: 
// 1. 此次递归调用所创建的新节点的哈希值
// 2. 删除操作之后,替换了被删除节点的节点
// 3. 如果删除的是某个子树的最左叶子节点,返回新的最左叶子节点的key字段
// 4. 被删除的值
// 5. 新产生的孤儿节点
func (tree *MutableTree) recursiveRemove(node *Node, key []byte) ([]byte, *Node, []byte, []byte, []*Node) {
	version := tree.version + 1

	if node.isLeaf() { // 递进到叶子节点
		if bytes.Equal(key, node.key) { // 找到了@key对应的节点
			return nil, nil, nil, node.value, []*Node{node}
		}
		return node.hash, node, nil, nil, nil // 没有找到@key对应的节点
	}

	if bytes.Compare(key, node.key) < 0 { // @key < node.key 进入左子树
		newLeftHash, newLeftNode, newKey, value, orphaned := tree.recursiveRemove(node.getLeftNode(tree.ImmutableTree), key)

		if len(orphaned) == 0 { // 没有找到@key对应的节点
			return node.hash, node, nil, value, orphaned
		} else if newLeftHash == nil && newLeftNode == nil { 
      // 只有高度为1的中间节点的左叶子节点被删除时才会进入该条件分支
      // 返回该节点的右孩子信息,则其父节点可以直接指向其右孩子
      // 删除左孩子后会引发某些中间节点(子树树根)的key字段变动
      // node.key == node.rightNode.key, 所以返回值第3个字段为node.key
      // NOTE v0.12.4中的实现中,此处有忘了处理将node加入orphans中
      // 参见 https://github.com/tendermint/iavl/pull/177
      // 将node加入orphans: *orphans = append(*orphans, node)
			return node.rightHash, node.rightNode, node.key, value, orphaned
		}
    // 成功删除@key对应的节点,从左孩子递归返回至高度>1的中间节点时,会进入该分支
		orphaned = append(orphaned, node) // 当前节点成为孤儿节点

		newNode := node.clone(version) // 根据当前节点生成新版本节点
		newNode.leftHash, newNode.leftNode = newLeftHash, newLeftNode
		newNode.calcHeightAndSize(tree.ImmutableTree) // 新版本节点重新赋值
		newNode, balanceOrphaned := tree.balance(newNode)

		return newNode.hash, newNode, newKey, value, append(orphaned, balanceOrphaned...)
	}
  // @key >= node.key, 进入右子树
	newRightHash, newRightNode, newKey, value, orphaned := tree.recursiveRemove(node.getRightNode(tree.ImmutableTree), key)

	if len(orphaned) == 0 { // 没有找到@key对应的节点
		return node.hash, node, nil, value, orphaned
	} else if newRightHash == nil && newRightNode == nil { 
    // 只有高度为1的中间节点的右叶子节点被删除时才会进入该条件分支
    // 返回该节点的左孩子信息,则其父节点可以直接指向其左孩子
    // 删除右孩子,不会影响路径上中间节点的key值,所以返回值的第3个字段为nil
    // NOTE v0.12.4中的实现中,此处有忘了处理将node加入orphans中
    // 参见 https://github.com/tendermint/iavl/pull/177
    // 将node加入orphans: *orphans = append(*orphans, node)
		return node.leftHash, node.leftNode, nil, value, orphaned
	}
	orphaned = append(orphaned, node)

	newNode := node.clone(version)
	newNode.rightHash, newNode.rightNode = newRightHash, newRightNode
	if newKey != nil { // 返回值newKey只有在删除某个左叶子节点时才不是nil
    // newKey是某棵子树的新的最左节点,即key最小的节点,需更新子树根节点key字段
		newNode.key = newKey
	}
	newNode.calcHeightAndSize(tree.ImmutableTree)
	newNode, balanceOrphaned := tree.balance(newNode)

	return newNode.hash, newNode, nil, value, append(orphaned, balanceOrphaned...)
}
```

相比`Set`方法的实现, `Remove`方法的实现相对复杂, 尤其是其中递归实现的`recursiveRemove`函数的5个返回值不容易理解. 5个返回值中的最后两个返回值容易理解,如果删除成功,则返回被删除的节点中的存储的值以及该删除操作产生的孤儿节点. 5个返回值中的前3个返回值则用来在递归调用之间传递信息以辅助由于删除节点而引发的树结构变化. 其中删除节点可能带来引发的中间节点的变化包括:

1. 中间节点的某个叶子被删除之后,该节点的父节点可直接指向该节点唯一的孩子节点
2. 某个左叶子节点被删除之后会引发某棵子树的树根节点中的key字段变化
3. 修改中间节点的值要求依次更新从该中间节点到树根的路径上所有的中间节点

`recursiveRemove`方法的5个返回值中的前3个是为完成上述中间节点的变动提供必要的信息. `recursiveRemove`方法会从根节点开始根据key值不断递归调用递进至叶子节点. 如果不存在需要删除的节点,就不会修改树结构. 如果找到了相应的叶子节点, 则演着相同的路径递归返回至根节点,并依次修改路径上的中间节点. 其中高度为1的中间节点需要特殊处理,因为删除叶子节点会导致该节点只剩一个孩子节点,此时该节点的父节点可以直接指向该节点仅剩的孩子节点, 此时前2个返回值就标记了仅剩的孩子节点的信息. 

前述的第2点变化说的是, 如果删除的是左叶子节点,则会引发某棵子树的根节点的key值的变化.子树的根节点key等于其右子树中叶子节点key的最小值,也即右子树中最左边的叶子节点的key. 基于同样的考量,删除左叶子节点之后, 第3个返回值便记录了相应的右叶子节点的key, 因为此时该右叶子节点就变成了某棵子树的最左叶子节点, 而删除右叶子节点时则无需做此种考虑,

对于前述的中间节点的第3点状态变化的修改则发生`recursiveRemove`方法输入参数中的节点高度大于1时. 如果是从左孩子节点返回则根据前2个返回值更新该节点的左孩子相关的状态. 如果是从右孩子节点返回则根据前2个返回值更新该节点的右孩子相关的状态. 此时需要注意的是,如果是从右孩子节点返回并且第3个参数的值不为`nil`, 则意味着以该节点为树根的子树中的最左叶子节点被删除,此时需要根据第3个返回参数更新该节点中的key. 从左孩子返回时则不需要考虑这种情况.

## IAVL+树的Merkle证明

在`Node`结构中加入左右孩子节点的哈希值,可以对IAVL+树中存储的键对应的值做存在性证明, 如果树中没有相应的键值对也可构建不存在性证明.由于IAVL+树中仅有叶子节点保存值, 所以对于一个键值对的存在性证明就是从树根到相应叶子节点的路径. 验证时只需要从叶子节点逐层计算哈希值并将最终得到的哈希值与已知的根节点的哈希值进行比对,如果相等就证明了该键值对在树中确实存在. 由于叶子节点按照从左到右的顺序键逐渐增大,则键值对的不存在性证明可以通过如下思路来完成: 在IAVL+树上确定目标键对应的叶子节点区间,并证明这些叶子节点的键不等于目标. 假设目标键为4, 而树中没有键为4的叶子节点,但是有键为3和5的叶子节点,则找到这两个叶子节点之后,通过证明这两个节点相邻的叶子节点并且键都不等于4, 就可以证明树中不存在键为4的值. 这就需要构建区间证明`RangeProof`. 实际上, 在IAVL+树的该实现中用文件`iavl/proof_range.go`文件中的`RangeProof`结构体统一了存在性证明与不存在性证明. 

```go
type RangeProof struct {
  LeftPath   PathToLeaf      // 树根到最左侧叶子节点的路径(不含叶子节点)
  InnerNodes []PathToLeaf    // 到其它叶子节点的路径
	Leaves     []proofLeafNode // Range包含的所有的叶子节点

	rootVerified bool   // 已经用合法的根哈希验证过该RangeProof
	rootHash     []byte // 当前RangeProof对应的根节点哈希,需rootVerified为true
	treeEnd      bool   // 最末叶子节点是树的最右叶子节点,需rootVerified为true
}
```

其中`PathToLeaf`是定义在文件`iavl/proof_path.go`中的`ProofInnerNode`的数组, 表示从根节点到某个叶子节点的路径, 不包括叶子节点, 而`proofInnerNode`是定义在文件`iavl/proof.go`中的结构体,仅包括在哈希值计算在过程中涉及到的中间节点的字段值, 该文件中同样定义了结构体`proofLeafNode`, 由于叶子节点的`height`和`size`字段都是固定值,需要包含在结构中. 而其中的`ValueHash`则是叶子节点存储的值的哈希值. `RangProof`结构体的构建较为复杂, 我们先讨论`PathToLeaf`的构建.

```go
type PathToLeaf []proofInnerNode

type proofInnerNode struct {
	Height  int8   `json:"height"`
	Size    int64  `json:"size"`
	Version int64  `json:"version"`
	Left    []byte `json:"left"` // 左孩子节点哈希值
	Right   []byte `json:"right"`// 右孩子节点哈希值
}

type proofLeafNode struct {
	Key       cmn.HexBytes `json:"key"`
	ValueHash cmn.HexBytes `json:"value"` 
	Version   int64        `json:"version"`
}
```

文件`iavl/proof.go`文件中的`PathToLeaf`方法可以根据键在树中构建从根节点到键所在叶子节点的路, 而具体的实现由函数`pathToLeaf`递归实现. 值得注意的是, 构成路径的中间节点在添加到路径中时, 如果该节点的左孩子节点是路径的一部分, 则当前`proofInnerNode`中的左孩子节点哈希值为`nil`; 如果该节点的右孩子节点是路径的一部分, 则当前`proofInnerNode`中的右孩子节点哈希值为`nil`. 这些为`nil`的哈希值可以根据路径中的其它节点计算而来. 

```go
func (node *Node) PathToLeaf(t *ImmutableTree, key []byte) (PathToLeaf, *Node, error) {
	path := new(PathToLeaf)
	val, err := node.pathToLeaf(t, key, path)
	return *path, val, err
}

func (node *Node) pathToLeaf(t *ImmutableTree, key []byte, path *PathToLeaf) (*Node, error) {
	if node.height == 0 {
		if bytes.Equal(node.key, key) {
			return node, nil
		}
		return node, errors.New("key does not exist")
	}

	if bytes.Compare(key, node.key) < 0 { // 应进入左子树
		pin := proofInnerNode{
			Height:  node.height,
			Size:    node.size,
			Version: node.version,
			Left:    nil, // 左孩子为空, 可根据路径计算
			Right:   node.getRightNode(t).hash,
		}
		*path = append(*path, pin) // 先添加当前节点再进入左子树
		n, err := node.getLeftNode(t).pathToLeaf(t, key, path)
		return n, err
	} // 进入右子树
	pin := proofInnerNode{
		Height:  node.height,
		Size:    node.size,
		Version: node.version,
		Left:    node.getLeftNode(t).hash,
		Right:   nil, // 右孩子为空, 可根据路径计算
	}
	*path = append(*path, pin) // 先添加当前节点再进入右子树
	n, err := node.getRightNode(t).pathToLeaf(t, key, path)
	return n, err
}
```

根据`pathToLeaf`的实现,当树中存在对应键的节点时, `PathToLeaf`返回的`PathToLeaf`中的第一个元素为根节点(下标为0), 最后一个元素则为目标叶子节点的父节点. 下图中展示了一个含有8个节点的二叉树, 其中大写字母用来指代具体的节点, 而每个节点中的数字表示该节点的键. 则`PathToLeaf`方法返回的关于键是`2`的节点的路径为`{A, B, E},J,nil`.  值得考虑的是, 当树中不存在对应键的节点时, 返回的`PathToLeaf`是什么? 上述实现根据`PathToLeaf`方法在下图展示的树中构建键为`2.5`的节点的路径为`{A, B, E}, J, err`. 

![tree](/Users/long/Downloads/plantuml/treePathToLeaf.png)

根据`PathToLeaf`以及叶子节点可以计算出根节点的哈希值,通过与已知的合法的根节点的哈希值比较即可完成验证, 这一逻辑在文件`iavl/proof_path.go`文件实现, 参见下面的代码, 这是`PathToLeaf`的方法,输入参数为相应的叶子节点以及已知的合法的根节点哈希值.

```go
func (pl PathToLeaf) verify(leafHash []byte, root []byte) error {
	hash := leafHash // 叶子节点开始逐层计算哈希值
	for i := len(pl) - 1; i >= 0; i-- {
		pin := pl[i]
		hash = pin.Hash(hash)
	}
	if !bytes.Equal(root, hash) { // 与已知的合法的根哈希值做比对
		return errors.Wrap(ErrInvalidProof, "")
	}
	return nil
}
```

接下来考察`RangeProof`的构造过程, 主要逻辑由`ImmutableTree` 的方法`getRangeProof`实现, 该方法的实现逻辑比较复杂, 但是粗线条的实现逻辑可以归纳为: 

1. 对输入参数做适当的检查并通过`t.root.hashWithCount()`完成树中所有的哈希计算
2. 通过`PathToLeaf`方法构建达到最左侧叶子节点的路径, 并保存路径和叶子节点至`RangeProof`结构体中
3. 根据`limit`和`keyEnd`判断是否可以终止,是则返回
4. 利用`t.root.traverseInRange`进行区间遍历,并在过程中保存区间中其它叶子节点和路径至`RangeProof`结构体中

```go
func (t *ImmutableTree) getRangeProof(keyStart, keyEnd []byte, limit int) (proof *RangeProof, keys, values [][]byte, err error) {
	// ... 省略部分参数检查
	t.root.hashWithCount() // Ensure that all hashes are calculated.
  
	path, left, err := t.root.PathToLeaf(t, keyStart) // 尝试获取keyStart对应节点的路径
	if err != nil { // keyStart不存在, 可以提供不存在性证明
		err = nil
	}
	startOK := keyStart == nil || bytes.Compare(keyStart, left.key) <= 0
	endOK := keyEnd == nil || bytes.Compare(left.key, keyEnd) < 0
	// If left.key is in range, add it to key/values.
	if startOK && endOK { // 找到的叶子节点在区间中, 保存相应的值
		keys = append(keys, left.key) // == keyStart
		values = append(values, left.value)
	}
	// Either way, add to proof leaves.
	var leaves = []proofLeafNode{ // 保存找到的叶子节点的信息
		{
			Key:       left.key,
			ValueHash: tmhash.Sum(left.value),
			Version:   left.version,
		},
	}

	_stop := false
	if limit == 1 {
		_stop = true // case 1 limit 是 1 
	} else if keyEnd != nil && bytes.Compare(cpIncr(left.key), keyEnd) >= 0 {
		_stop = true // case 2 keyEnd = left.Key + 1
	}
	if _stop { // 可以终止, 直接返回
		return &RangeProof{
			LeftPath: path,
			Leaves:   leaves,
		}, keys, values, nil
	}

	// Get the key after left.key to iterate from.
	afterLeft := cpIncr(left.key) // 没有返回, 键递增1, 继续查找叶子节点

	// Traverse starting from afterLeft, until keyEnd or the next leaf
	// after keyEnd.
	var innersq = []PathToLeaf(nil) // 保存后续叶子节点路径中新的中间节点
	var inners = PathToLeaf(nil)
	var leafCount = 1 // 记录保存的叶子节点的数目, 已经保存了最左侧叶子节点
	var pathCount = 0 // 
	// var keys, values [][]byte defined as function outs.
	// 从afterLeft进行区间遍历直到keyEnd或者keyEnd的下一个叶子节点, 终止条件有传入的函数判断
  // 区间中的叶子节点会共享一些中间节点, RangeProof不保存重复的中间节点
  // 单独存储的取件中最左侧叶子节点的路径为RangeProof的这种构造过程提供指导信息
	t.root.traverseInRange(t, afterLeft, nil, true, false, 0,
		func(node *Node, depth uint8) (stop bool) { 

			// Track when we diverge from path, or when we've exhausted path,
			// since the first innersq shouldn't include it.
			if pathCount != -1 { // 分支1
				if len(path) <= pathCount { // 分支1-1
					// We're done with path counting.
					pathCount = -1
				} else { // 分支1-2
					pn := path[pathCount]
					if pn.Height != node.height ||
						pn.Left != nil && !bytes.Equal(pn.Left, node.leftHash) ||
						pn.Right != nil && !bytes.Equal(pn.Right, node.rightHash) { // 分支1-2-1

						// 条件判断为真,意味着前序遍历与左侧相邻叶子节点的路径的中间节点产
						pathCount = -1 
					} else { // 分支1-2-2
						pathCount++
					}
				}
			}

			if node.height == 0 { // 遍历至新的叶子节点, 分支3
				innersq = append(innersq, inners) // 保存路径中新增的中间节点, 可能为空
				inners = PathToLeaf(nil)
				leaves = append(leaves, proofLeafNode{ // 保存叶子节点信息
					Key:       node.key,
					ValueHash: tmhash.Sum(node.value),
					Version:   node.version,
				})
				leafCount++ // 更新叶子节点计数器
				// Maybe terminate because we found enough leaves.
				if limit > 0 && limit <= leafCount { 
					return true // 检查终止条件, 找到足够多叶子节点
				}
				// Terminate if we've found keyEnd or after.
				if keyEnd != nil && bytes.Compare(node.key, keyEnd) >= 0 {
					return true // 检查终止条件, 遍历至keyEnd或者已经超过keyEnd
				}
				// Value is in range, append to keys and values.
				keys = append(keys, node.key) // 叶子节点在范围中, 记录叶子节点信息
				values = append(values, node.value)
				// Terminate if we've found keyEnd-1 or after.
				// We don't want to fetch any leaves for it.
				if keyEnd != nil && bytes.Compare(cpIncr(node.key), keyEnd) >= 0 {
					return true
				}
			} else { // 分支4
				// Inner node.
				if pathCount >= 0 { // 分支4-1
					// Skip redundant path items.
				} else { // 分支4-2
					inners = append(inners, proofInnerNode{
						Height:  node.height, // Left 字段为 nil, 因为是在从左到右构建
						Size:    node.size,   // 因为在按照从左到右的顺序构建叶子节点的路径
						Version: node.version,// 并且提前构建了最左侧叶子节点的路径
						Left:    nil,         // 意味着后续叶子节点的中间路径的左孩子哈希值
						Right:   node.rightHash, // 都可以根据已保存的中间和叶子节点进行计算
					})
				}
			}
			return false
		},
	)

	return &RangeProof{
		LeftPath:   path,
		InnerNodes: innersq,
		Leaves:     leaves,
	}, keys, values, nil
}
```

`getRangeProof`实现中最难理解的是第4步的计算, 尤其是有`pathCount`相关的部分, 为了方便阐述这部分的逻辑,在代码注释中对不同的分支添加了标记. 为了理解第4步需要记住的是, 在该函数之前已经构建并保存了区间最左侧叶子节点的路径, 第4步是为了根据这一路径构建区间中其它叶子节点的路径并且需要达到不重复存储相同中间节点的效果. 第4步中就是为了借助`t.root.traverseInRange`的前序遍历功能完成这一目标. 最左侧叶子节点的`PathToLeaf`和区间中的第2个叶子节点的`PathToLeaf`会共享从根节点开始的多个中间节点. 

还是以8个叶子节点的树为例,假设是在构造键属于`[2, 6]`范围`RangeProof`,其中所有叶子节点的`PathToLeaf`的路径上的中间节点用不同的图形表示. 最左侧叶子节点`J`对应的路径为`{A, B, E}`, 而`traverseInRange`遍历到第2个叶子节点`K`经过的中间路径也为`{A, B, E}`. 因此在``getRangeProof`的计算时,会不断进入"分支1-2-2"对`pathCount`进行累加, 每次累加之后会进入"分支4-1"不做任何计算. 当`pathCount`的值变为3后, `traverseInRange`访问的下一个节点是`K`, 此时会进入"分支1-1"执行`pathCount=-1`, 可以注意到的是一但`pathCount`的值变为-1之后,函数中没有任何地方会再修改该变量的值,也即"分支1"不会再执行. 此时在访问叶子节点所以会进入"分支3",保存节点`K`的信息以及其`PathToLeaf`引入的新的中间节点. 由于叶子节点`K`没有引入新的中间节点,所以叶子节点在`RangeProof`中对应的`PathToLeaf`为`nil`. 接下来由于只会进入"分支3"和"分支4-2", 借助`traverseInRange`的前序遍历, 中间节点被会加入到`PathToLeaf`中,而每碰到叶子节点就保存节点以及对应的`PathToLeaf`. 因此下图中构建的`RangeProof`的最终值为: `LeftPath  = {A, B, E},InnderNodes = {{}, {C, F}, {}, {G}}, Leaves = {J, K, L, M, N}`.

![treeRangeProof26](/Users/long/Downloads/plantuml/treeRangeProof26.png)

`ImmutableTree`的`GetWithProof`方法封装了`getRangeProof`函数. 该方法输入参数为目标键, 当键在树中时返回对应的值,当键不在树中时返回`nil`,两种情况下均用参数`start, end, limit = @key, @key+1, 2`构建`RangeProof`, 可以在随后用来做(不)存在性证明. 

```go
func (t *ImmutableTree) GetWithProof(key []byte) (value []byte, proof *RangeProof, err error) {
	proof, _, values, err := t.getRangeProof(key, cpIncr(key), 2)
	if err != nil {
		return nil, nil, errors.Wrap(err, "constructing range proof")
	}
	if len(values) > 0 && bytes.Equal(proof.Leaves[0].Key, key) {
		return values[0], proof, nil
	}
	return nil, proof, nil
}
```

 (不)存在性证明需要能够根据证明本身构建出根节点的哈希值并与已知的合法的根哈希值进行比对. 定义在文件`iavl/proof_range.go`中的`RangeProof`方法`_computeRootHash`可以基于该结构体计算出根节点的哈希值, 此处不再具体介绍实现原理. `computeRootHash`封装了`_computeRootHash`并且在没有错误的情况下记录`rootHash`和`treeEnd`.而与根节点哈希值的比对则在`RangeProof`的方法`verify`中完成, 而方法`Verify`则进一步封装了`verify`方法.

```go
func (proof *RangeProof) computeRootHash() (rootHash []byte, err error) {
	rootHash, treeEnd, err := proof._computeRootHash()
	if err == nil {
		proof.rootHash = rootHash // memoize
		proof.treeEnd = treeEnd   // memoize
	}
	return rootHash, err
}

func (proof *RangeProof) verify(root []byte) (err error) {
	rootHash := proof.rootHash
	if rootHash == nil {
		derivedHash, err := proof.computeRootHash()
		if err != nil {
			return err
		}
		rootHash = derivedHash
	}
	if !bytes.Equal(rootHash, root) {
		return errors.Wrap(ErrInvalidRoot, "root hash doesn't match")
	} else {
		proof.rootVerified = true
	}
	return nil
}
```

在深入了解`RangeProof`结构的定义, 构造与验证之后,可以讨论对于键值对的存在性证明和不存在证明了, 两种证明的分别在`RangeProof`的方法`VerifyItem`和`VerifyAbsence`中实现. `VerifyItem`的输入参数为键值对, 这个函数的目标是利用`RangeProof`做该键值对的存在性证明, 具体实现逻辑参见代码中的注释.

```go
func (proof *RangeProof) VerifyItem(key, value []byte) error {
	leaves := proof.Leaves
	if proof == nil {
		return errors.Wrap(ErrInvalidProof, "proof is nil")
	}
	if !proof.rootVerified { // RangeProof必须已经用合法的树根节点哈希验证过
		return errors.New("must call Verify(root) first")
	}
	i := sort.Search(len(leaves), func(i int) bool { // 二分查找@key
		return bytes.Compare(key, leaves[i].Key) <= 0
	})
	if i >= len(leaves) || !bytes.Equal(leaves[i].Key, key) { // RangeProof未找到键@key
		return errors.Wrap(ErrInvalidProof, "leaf key not found in proof")
	}
	valueHash := tmhash.Sum(value)
	if !bytes.Equal(leaves[i].ValueHash, valueHash) { // 比较@value的哈希值
		return errors.Wrap(ErrInvalidProof, "leaf value hash not same")
	}
	return nil
}
```

对比之下, 不存在证明的方法`VerifyAbsence`的实现逻辑较为复杂. 其主要逻辑可以归纳为如果目标键小于树的最左叶子节的键, 或者大于树的最右叶子节点的键, 或者目标键位于`RangeProof`中2个叶子节点的键之间, 则树中不存在目标键.

```go
func (proof *RangeProof) VerifyAbsence(key []byte) error {
	if proof == nil {
		return errors.Wrap(ErrInvalidProof, "proof is nil")
	}
	if !proof.rootVerified { // 首先需要验证RangeProof自身合法
		return errors.New("must call Verify(root) first")
	}
	cmp := bytes.Compare(key, proof.Leaves[0].Key) // 至少含有一个叶子节点
	if cmp < 0 { // 如果@key小于键最小的叶子节点
		if proof.LeftPath.isLeftmost() { // 且该节点为IAVL+树中的最左叶子节点
			return nil // 则IAVL+树中不存在@key
		} else {
			return errors.New("absence not proved by left path")
		}
	} else if cmp == 0 { // 如果相等的话, @key在树中
		return errors.New("absence disproved via first item #0")
	} // @key > proof.Leaves[0].Key
	if len(proof.LeftPath) == 0 { // 树中只有根节点时, PathToLeaf为空
		return nil // proof ok
	}
	if proof.LeftPath.isRightmost() { // @key大于树的最右叶子节点
		return nil // 也即@key不在树中
	}

  // 尝试找到第一个键大于@key的叶子节点
	for i := 1; i < len(proof.Leaves); i++ {
		leaf := proof.Leaves[i]
		cmp := bytes.Compare(key, leaf.Key)
		if cmp < 0 { // @key < leaf.key
			return nil // proof ok 
		} else if cmp == 0 { // @key等于叶子节点的键,存在于树中
			return errors.New(fmt.Sprintf("absence disproved via item #%v", i))
		} else { // @key > leaf.key
			continue
		}
	}
	
  // 执行到这里意味着@key大于RangeProof中所有叶子节点的键
	if proof.treeEnd { // 如果最后一个叶子节点是树的最右叶子节点
		return nil // OK! @key不存在于树中
	}
	
	if len(proof.Leaves) < 2 { // 至少需要2个叶子节点才可进行不存在性证明
		return errors.New("absence not proved by right leaf (need another leaf?)")
	} else {
		return errors.New("absence not proved by right leaf")
	}
}
```

至此已经介绍了`RangeProof`的定义,构造以及用来做(不)存在性证明的原理与实现. 文件`iavl/proof_iavl_value.go`和`iavl/proof_iavl_absence.go`中基于`RangeProof`定义了相应的结构体`IAVLValueOp`和`IAVLAbsenceOp`. 在两个结构体都有的`Run`方法中会分别调用`RangeProof`的`VerifyItem`和`VerifyAbsence`方法验证存在性与不存在性. 这两个结构体都实现了`ProofOp`, `GetKey`以及`Run`方法, 也就实现了[Tendermint](https://github.com/tendermint/tendermint/tree/v0.32)项目中的`ProofOperator`接口, 参考的是v0.32.9版本中的文件`tendermint/crypto/merkle/proof.go`文件. 其中的`ProofOp`是定义在`tendermint/crypto/merkle/merkle.pb.go`文件中结构体. `ProofOperator`接口的存在使得Tendermint项目中在相同的接口下可以根据需求在不同的场景采用不同的可认证数据结构. 例如对于不断更新的账户余额等信息使用IAVL+树, 而每个区块中的交易等生成之后不会再变化则可以采用类似于Bitcoin中的Merkle树(`tendermint/crypto/merkle`文件下有相关的实现,此处不再讲解)

```go
type IAVLValueOp struct {
	key []byte // Encoded in ProofOp.Key.
	Proof *RangeProof `json:"proof"`
}

type IAVLAbsenceOp struct {
	// Encoded in ProofOp.Key.
	key []byte
	Proof *RangeProof `json:"proof"`
}

// tendermint/crypto/merkle/proof.go
type ProofOperator interface {
	Run([][]byte) ([][]byte, error)
	GetKey() []byte
	ProofOp() ProofOp // dingyile
}

// tendermint/crypto/merkle/merkle.pb.go
type ProofOp struct {
	Type                 string   
	Key                  []byte   
	Data                 []byte   
  // ... 
}
```

## 在COSMOS-SDK中的应用

[cosmos-sdk v0.37.5](https://github.com/cosmos/cosmos-sdk/tree/v0.37.5)的`cosmos-sdk/store/iavl/store.go`文件中将IAVL+树封装成`Store`结构体, 其中的`tree Tree`字段为接口, 前述的IAVL+树的实现满足了接口结束, 而`numRecent`和`storeEvery`则与剪枝操作相关,随后再介绍.

```go
type Store struct {
	tree Tree // Tree接口, 前述的IAVL+树实现了相应的接口
	numRecent int64 // 保存多少个老版本, 0意味着不保存老版本

	// This is the distance between state-sync waypoint states to be stored.
	// See https://github.com/tendermint/tendermint/issues/828
	// A value of 1 means store every state.
	// A value of 0 means store no waypoints. (node cannot assist in state-sync)
	// By default this value should be set the same across all nodes,
	// so that nodes can know the waypoints their peers store.
	storeEvery int64 // 
}
```

接下来关注cosmos-sdk中基于`Store`结构体的写操作,也即将IAVL+树持久化到数据库中. 持久化过程是通过cosmos-sdk的ABCI接口的`Commit()`方法触发的, `Commit`会依次出发各个子模块的`func (st *Store) Commit() types.CommitID `操作, 从而触发IAVL+树的`SaveVersion`操作, 参见下面的代码. 保存完成之后, 则会根据剪枝策略判断是否需要进行删除操作, 并根据需要触发IAVL+树的`DeleteVersion`操作.

```go
func (st *Store) Commit() types.CommitID {
	// Save a new version.
	hash, version, err := st.tree.SaveVersion() //持久化working tree
	if err != nil {
		// TODO: Do we want to extend Commit to allow returning errors?
		panic(err)
	}

	// Release an old version of history, if not a sync waypoint.
	previous := version - 1 // 剪枝操作相关的逻辑
	if st.numRecent < previous { // 超过numRecent个版本之后, 每个版本都需剪枝检查
		toRelease := previous - st.numRecent // 需要剪枝的版本号
		if st.storeEvery == 0 || toRelease%st.storeEvery != 0 {
      // 不需要存储每个版本或者该版本不是storeEvery指定的需要额外存储的历史版本
			err := st.tree.DeleteVersion(toRelease) // 删除版本
			if errCause := errors.Cause(err); errCause != nil && errCause != iavl.ErrVersionDoesNotExist {
				panic(err)
			}
		}
	}

	return types.CommitID{
		Version: version,
		Hash:    hash,
	}
}
```

IAVL+树的`DeleteVersion`方法值得详细介绍, 经过必要的参数检查之后会出发数据库的`DeleteVersion`操作并通过底层数据库的`Commit`方法持久化本次修改. 而底层数据库的`DeleteVersion`会首先删除特定版本的孤儿节点然后删除版本对应的整棵树. 删除特定版本的孤儿节点时，只会删除从下一个版本开始不会再被引用的那些节点，以及那些在之前的并未删除的版本中不会被引用的节点. Tendermint关于IAVL+树的实现是支持跳跃式删除版本的，但cosmos-sdk在使用的时候其减枝策略是按照版本号从小到大依次删除的. 这里可以回一下孤儿节点在存储时的键格式: `o||toVersion||fromVersion||node.hash`, 对于完成这一按照不再引用的孤儿节点操作大有裨益.

```go
func (tree *MutableTree) DeleteVersion(version int64) error {
   if version == 0 {
      return errors.New("version must be greater than 0")
   }
   if version == tree.version { // 不能删除最新版本
      return errors.Errorf("cannot delete latest saved version (%d)", version)
   }
   if _, ok := tree.versions[version]; !ok { 
      return errors.Wrap(ErrVersionDoesNotExist, "")
   }

   tree.ndb.DeleteVersion(version, true) // 触发数据库的DeleteVersion操作
   tree.ndb.Commit() // 持久化修改

   delete(tree.versions, version) // 从IAVL+树结构中删除相应的版本

   return nil
}
// DeleteVersion deletes a tree version from disk.
func (ndb *nodeDB) DeleteVersion(version int64, checkLatestVersion bool) {
	ndb.mtx.Lock()
	defer ndb.mtx.Unlock()

	ndb.deleteOrphans(version) // 删除孤儿节点
	ndb.deleteRoot(version, checkLatestVersion) // 删除目标版本的整棵树
}
```

## COSMOS-SDK中的剪枝选项

前面我们看到cosmos-sdk在封装IAVL+树时,在`Store`结构体中额外包含了与剪枝操作相关的两个字段`numRecent`和`storeEvery`. 这两个字段对应cosmos-sdk中剪枝选项结构体`PruningOptions`中的两个字段`keepRecent`和`keepEvery`, 参见文件`cosmos-sdk/store/types/pruning.go`.

- `keepRecent`:  出当前版本之外,保存多少个最近的历史版本
- `keepEvery`:  在`keepRecent`之外额外保存一些旧版本, 每隔`keepEvery`个版本保存一次

```go
type PruningOptions struct {
	keepRecent int64 // 保存多少个最近的历史版本
	keepEvery  int64 // 额外保存一些旧版本, 每隔`keepEvery`个版本保存一次
}

// default pruning strategies
var (
	// PruneEverything - 只保存最新版本, 所有历史版本都删除
	PruneEverything = NewPruningOptions(0, 0)
	// PruneNothing - 保存所有历史状态, 不删除任何东西
	PruneNothing = NewPruningOptions(0, 1)
  // PruneSyncable - 只保持最近的100个区块,并且再次之外每隔10000个版本保存一次
	PruneSyncable = NewPruningOptions(100, 10000)
)
```

cosmosd-sdk预置了3种剪枝选项, `PruneEverything`, `PruneNothing`, `PruneSyncable`, 具体含义参加下面的代码. 如果不特定指定剪枝选项, 默认为`PruneSyncable`. 在此基础上,可以理解`func (st *Store) Commit() types.CommitID`函数体中后半段的剪枝操作, 参见前面的代码注释. 下面以一个测试用例展示具体的剪枝操作. 这个例子中`keepRecent=5, keepEvery=3`.可以看到在不超过5个旧版本的状态时, 不会发生剪枝. 在最新版本为`curr`时, 首先计算`previous`的版本号`previous=curr-1`, 随后计算`toRelease = (previous-keepRecent)`以及`target%keepEvery==0`是否成立，如果成立，则不进行剪枝，否则就将`target`对应的版本状态给删除。

```go
type pruneState struct {
	stored  []int64
	deleted []int64
}

func TestIAVLDefaultPruning(t *testing.T) {
	//Expected stored / deleted version numbers for:
	//numRecent = 5, storeEvery = 3
	var states = []pruneState{
		{[]int64{}, []int64{}},
		{[]int64{1}, []int64{}},
		{[]int64{1, 2}, []int64{}},
		{[]int64{1, 2, 3}, []int64{}},
		{[]int64{1, 2, 3, 4}, []int64{}},
		{[]int64{1, 2, 3, 4, 5}, []int64{}},
		{[]int64{1, 2, 3, 4, 5, 6}, []int64{}},
		{[]int64{2, 3, 4, 5, 6, 7}, []int64{1}},
		{[]int64{3, 4, 5, 6, 7, 8}, []int64{1, 2}},
		{[]int64{3, 4, 5, 6, 7, 8, 9}, []int64{1, 2}},
		{[]int64{3, 5, 6, 7, 8, 9, 10}, []int64{1, 2, 4}},
		{[]int64{3, 6, 7, 8, 9, 10, 11}, []int64{1, 2, 4, 5}},
		{[]int64{3, 6, 7, 8, 9, 10, 11, 12}, []int64{1, 2, 4, 5}},
		{[]int64{3, 6, 8, 9, 10, 11, 12, 13}, []int64{1, 2, 4, 5, 7}},
		{[]int64{3, 6, 9, 10, 11, 12, 13, 14}, []int64{1, 2, 4, 5, 7, 8}},
		{[]int64{3, 6, 9, 10, 11, 12, 13, 14, 15}, []int64{1, 2, 4, 5, 7, 8}},
	}
	testPruning(t, int64(5), int64(3), states)
}
```

## 小结

本文中首先介绍了IAVL+树的设计理念, 以及树中节点的结构与每个字段的含义. 结合`Node`定义以及底层数据库, 介绍了IAVL+树数据库中的存储. 随后介绍了对IAVL+树读写和遍历操作. 随后介绍了作为可认证数据结构的IAVL+树所支持的键值对的存在性证明以及键的不存在性证明, 以及Tendermint项目中基于该结构所支持的数据认证操作. 最后我们介绍了cosmos-sdk对IAVL+树的封装以及所支持的剪枝操作. 