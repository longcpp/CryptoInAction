# COSMOS-SDK中的存储实现

基于账户模型的cosmos-sdk中需要可认证数据结构(Authenticated Data Structure, ADS)来存储包括账户的状态信息在内的各类信息. 以太坊中使用Merkle Patricia Tree (MPT树)数据结构来提供相应的功能, 而cosmos-sdk另辟蹊径通过组合Mekle树和自平衡的二叉搜索树构建了新型的ADS数据结构IAVL+树. 本文首先介绍IAVL+树结构的概念和实现, 然后分析这一数据结构在cosmos-sdk中的应用. IAVL+树的实现参照`github.com/tendermint/iavl`的[`v0.12.4`](https://github.com/tendermint/iavl/tree/v0.12.4)版本, cosmos-sdk的实现参考`github.com/cosmos/cosmos-sdk`的[`v0.37.5`](https://github.com/cosmos/cosmos-sdk/tree/v0.37.5)版本.

## 可认证数据结构IAVL+树

IAVL+是cosmos-sdk中各个模块所依赖的`KVStore`的底层实现, 全称为"Immutable AVL +"树. 其设计目标是为键值对(例如账户余额)提供可持久化存储的能力, 同时支持版本化以及快照功能. IAVL+树中的节点是不可修改的(Immutable)并且通过节点的哈希值来进行索引.如果节点不可修改, 如何更新的节点的状态以反映存储状态的变化? 在IAVL+树中修改某个节点时, 会先生成一个新的节点, 然后用该节点来替换目标节点. 这种更新方式配合在节点中保存的版本信息, 就同时实现了版本化和生成快照的功能, 也就支持了状态版本之间的快速切换. IAVL+树是基于AVL树构建而来的. 在AVL树中, 任意节点的左右子树的高度最多相差1. 当插入/删除做到导致某个节点的左右子树高度差值大于1时, 会出发自平衡操作. AVL树中通常叶子节点和中间节点都可以存储键值对, 而AVL+树通过修改AVL树使得仅有叶子节点存储键值对, 而中间节点仅用来存储键以及左右子树的信息. 这种改动可以简化数据结构的实现. IAVL+树继承了AVL树的特性: 自平衡的二叉搜索树, 对于n个叶子节点的查找/插入/删除操作的时间复杂度都为O(logn), 在新增或删除Node时可能会触发一次或者多次树的旋转操作以保证树的平衡.

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

虽然叶子节点和中间节点复用了相同的数据结构`Node`, 但是由于字段值的不同, 两种节点的哈希值计算过程也不相同:

- 计算叶子节点哈希值: `Hash(height||size||version||key||value)`
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

有了`Node`结构体和`nodeDB`, IAVL+树的定义在文件`iavl/immutable_tree.go`的结构体`ImmutableTree`中.  结构体比较简单, 只包括指向IAVL+树的根节点的指针`root`, 存储树中所有`Node`的数据库`ndb`以及这棵树的版本号. 如前所述, 每个区块执行完成之后都会形成一个新的IAVL+树来保存最新的状态集合, 而一次状态更新都会导致原先版本的IAVL+树中的一些节点被替换下来, 这些被替换下来的`Node`成为孤儿节点. 默认配置下, 所有的节点包括对应每个IAVL+树版本的根节点, 版本更新中形成的孤儿节点以及新生成的节点都会被`nodeDB`持久化到数据库中, 这就需要`nodeDB`的读写过程中能够区分三种类型的节点.

```go
type ImmutableTree struct {
	root    *Node // 指向根节点的指针
	ndb     *nodeDB // 存储节点的数据库
	version int64 // 该树的版本号
}
```

`github.com/tendermint/iavl`项目中为3类`Node`在存储到数据库中时, 定义了不同的键格式. 根节点, 其它节点及孤儿节点的键格式分别以字符`r`, `o`和`n`开始,并在后面级联不同的字段, 具体如下:

- 根节点的键格式: `r||<version>`
- 其它节点的键格式: `n||<node.hash>`
- 孤儿节点的键格式: `o||toVersion||fromVersion||hash`

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
      // todo 该节点应加入孤儿节点,这里并没有将该节点放入孤儿节点的操作
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
    // todo 该节点应该加入孤儿节点才对,但是这里并没有将该节点放入孤儿节点的操作
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



## COSMOS-SDK中的存储实现

![](/Users/long/Git/github/longcpp/CryptoInAction/cosmos/store-types.svg)

## COSMOS-SDK中的剪枝功能



