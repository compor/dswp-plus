//1st step: create dependence graph

#include "DSWP.h"

using namespace llvm;
using namespace std;

void DSWP::addControlDependence(BasicBlock *a, BasicBlock *b) {
	//TODO: dinstingush the loop carried

	Instruction *u = &a->getInstList().back();
	Instruction *v = b->getFirstNonPHI();
	addEdge(u, v, CONTROL);
}

void DSWP::checkControlDependence(BasicBlock *a, BasicBlock *b, PostDominatorTree &pdt) {
	BasicBlock *lca = pdt.findNearestCommonDominator(a, b);

	cout << a->getName().str() << " " << b->getName().str() << " " << lca->getName().str() << endl;

	if (lca == pre[a]) {	//case 1
		BasicBlock *BB = b;
		while (BB != lca) {
			addControlDependence(a, BB);
			BB = pre[BB];
		}
	} else if (lca == a) {	//case 2: capture loop dependence
		BasicBlock *BB = b;
		while (BB != pre[a]) {
			cout << "\t" << a->getName().str() << " " << BB->getName().str() << endl;
			addControlDependence(a, BB);
			BB = pre[BB];
		}
	} else {
		error("unknow case in checkControlDependence!");
	}
}

void DSWP::dfsVisit(BasicBlock *BB, std::set<BasicBlock *> &vis,
					std::vector<BasicBlock *> &ord, Loop *L) {
	vis.insert(BB); //Mark as visited
	for (succ_iterator SI = succ_begin(BB), E = succ_end(BB); SI != E; ++SI)
		if (L->contains(*SI) && vis.find(*SI) == vis.end())
			dfsVisit(*SI, vis, ord, L);

	ord.push_back(BB);
}

void DSWP::buildPDG(Loop *L) {
	cout<<">>Building PDG for new loop"<<endl;
	cout<<">>Enumerating blocks"<<endl;
	raw_os_ostream outstream(cout);

    //Initialize PDG
	for (Loop::block_iterator bi = L->getBlocks().begin(); bi != L->getBlocks().end(); bi++) {
		BasicBlock *BB = *bi;
		cout<<">>BASIC BLOCK ("<<BB->getName().str()<<")"<<endl;
		BB->print(outstream);
		cout<<endl<<endl;
		for (BasicBlock::iterator ui = BB->begin(); ui != BB->end(); ui++) {
			Instruction *inst = &(*ui);

			//standardlize the name for all expr
			if (util.hasNewDef(inst)) {
				inst->setName(util.genId());
				dname[inst] = inst->getName().str();
			} else {
				dname[inst] = util.genId();
			}

			pdg[inst] = new vector<Edge>();
			rev[inst] = new vector<Edge>();
		}
	}
	
	cout<<">>End basic blocks"<<endl;

	//LoopInfo &li = getAnalysis<LoopInfo>();

	/*
	 * Memory dependency analysis
	 */
	MemoryDependenceAnalysis &mda = Pass::getAnalysis<MemoryDependenceAnalysis>();

	for (Loop::block_iterator bi = L->getBlocks().begin(); bi != L->getBlocks().end(); bi++) {
		BasicBlock *BB = *bi;
		for (BasicBlock::iterator ii = BB->begin(); ii != BB->end(); ii++) {
			Instruction *inst = &(*ii);

			//data dependence = register dependence + memory dependence

			//begin register dependence
			for (Value::use_iterator ui = ii->use_begin(); ui != ii->use_end(); ui++) {
				if (Instruction *user = dyn_cast<Instruction>(*ui)) {
					if (L->contains(user)) {
						cout<<">>REG dependency: [[";
						inst->print(outstream);
						cout<<"]] -> [[";
						user->print(outstream);
						cout<<"]]"<<endl;
						addEdge(inst, user, REG);
					}
				}
			}
			//finish register dependence

			//begin memory dependence
			MemDepResult mdr = mda.getDependency(inst);
			//TODO not sure clobbers mean!!

			if (mdr.isDef()) {
				Instruction *dep = mdr.getInst();

				if (isa<LoadInst>(inst)) {
					//READ AFTER WRITE
					if (isa<StoreInst>(dep)) {
						cout<<">>MEM read after write (true) dependency: [[";
						dep->print(outstream);
						cout<<"]] -> [[";
						inst->print(outstream);
						cout<<"]]"<<endl;
						addEdge(dep, inst, DTRUE);
					}
					//READ AFTER ALLOCATE
					if (isa<AllocaInst>(dep)) {
						cout<<">>MEM read after allocate (true) dependency: [[";
						dep->print(outstream);
						cout<<"]] -> [[";
						inst->print(outstream);
						cout<<"]]"<<endl;
						addEdge(dep, inst, DTRUE);
					}
				}
				if (isa<StoreInst>(inst)) {
					//WRITE AFTER READ
					if (isa<LoadInst>(dep)) {
						cout<<">>MEM write after read (anti) dependency: [[";
						dep->print(outstream);
						cout<<"]] -> [[";
						inst->print(outstream);
						cout<<"]]"<<endl;
						addEdge(dep, inst, DANTI);
					}
					//WRITE AFTER WRITE
					if (isa<StoreInst>(dep)) {
						cout<<">>MEM write after write (out) dependency: [[";
						dep->print(outstream);
						cout<<"]] -> [[";
						inst->print(outstream);
						cout<<"]]"<<endl;
						addEdge(dep, inst, DOUT);
					}
					//WRITE AFTER ALLOCATE
					if (isa<AllocaInst>(dep)) {
						cout<<">>MEM write after allocate (out) dependency: [[";
						dep->print(outstream);
						cout<<"]] -> [[";
						inst->print(outstream);
						cout<<"]]"<<endl;
						addEdge(dep, inst, DOUT);
					}
				}
				//READ AFTER READ IS INSERT AFTER PDG BUILD
			}
			//end memory dependence
		}//for ii
	}//for bi

	cout<<">>Finished finding data dependences"<<endl;


	/* 
	 *
	 * Topologically sort blocks and create peeled loop
	 *
	 */

	cout<<">>Finding control dependences"<<endl;

	BasicBlock *curheader = L->getHeader();
	Function *curfunc = header->getParent();
	Module *curmodule = curfunc->getParent();
	LLVMContext *curcontext = &curmodule->getContext();
	FunctionType *functype = FunctionType::get(Type::getVoidTy(*curcontext), false); 
	Function *ctrlfunc = Function::Create(functype, Function::ExternalLinkage,
										  "dummyloopunroll", module);
	Function &ctrlfuncref = *ctrlfunc;

	std::set<BasicBlock *> b_visited;
	std::vector<BasicBlock *> bb_ordered;
	std::vector<BasicBlock *> dummylist;

	// DFS to (reverse) topologically sort the basic blocks
	b_visited.clear();
	for (Loop::block_iterator bi = L->getBlocks().begin(),
					be = L->getBlocks().end(); bi != be; bi++) {
		BasicBlock *BB = *bi;
		if (b_visited.find(BB) == b_visited.end()) //Not visited 
			dfsVisit(BB, b_visited, bb_ordered, L);
	}

	cout<<">>Reverse topological sort:"<<endl;
	assert(!bb_ordered.empty());
	for (std::vector<BasicBlock *>::iterator it = bb_ordered.begin(); it !=
		bb_ordered.end(); ++it) {
	   cout<<(*it)->getName().str()<<", ";
	}
	cout<<endl;

	std::map<BasicBlock *, std::pair<BasicBlock *, BasicBlock *> > realtodummy;
	std::map<BasicBlock *, BasicBlock *> dummytoreal;
	std::map<BasicBlock *, unsigned int> instnum;
	LLVMContext &ctxt = ctrlfunc->getParent()->getContext();

	//Create dummy basic blocks and populate lookup tables
	if (!bb_ordered.empty()) {
		std::vector<BasicBlock *>::iterator it = bb_ordered.end();
		unsigned int i = 0;
		do {
			--it;
			//Dummy block for first iteration
			const std::string str1 = "tophalf_" + (*it)->getName().str(); 
			const std::string str2 = "bottomhalf_" + (*it)->getName().str(); 
			const Twine n1(str1);
			const Twine n2(str2);
			cout<<"n1 = "<<n1.str()<<", n2 = "<<n2.str()<<endl;
			BasicBlock *newbb = BasicBlock::Create(ctxt,
												   n1, ctrlfunc, 0); 
			//Dummy block for second iteration
			BasicBlock *newbb2 = BasicBlock::Create(ctxt,
												   n2, ctrlfunc, 0); 

			dummylist.push_back(newbb);
			dummylist.push_back(newbb2);

			//Update lookup tables
			realtodummy[*it] = std::make_pair(newbb, newbb2);
			dummytoreal[newbb] = *it;
			dummytoreal[newbb2] = *it;
			instnum[*it] = i;
			i++;
		} while (it != bb_ordered.begin());
	}

	cout<<">>Printing out names of dummy blocks inside our fake function"<<endl;
	for (Function::iterator FI = ctrlfunc->begin(), FE = ctrlfunc->end();
		 FI != FE; ++FI) {
		cout<<(*FI).getName().str()<<", ";
	}
	cout<<endl;

	//Add branch instructions for dummy blocks
	if (!bb_ordered.empty()) {
		std::vector<BasicBlock *>::iterator it = bb_ordered.end();
		do {
			--it;
			std::pair<BasicBlock *, BasicBlock *> dummypair = realtodummy[*it];
			BasicBlock *bbdummy1 = dummypair.first; 
			BasicBlock *bbdummy2 = dummypair.second;
			IRBuilder<> builder1(bbdummy1);
			IRBuilder<> builder2(bbdummy2);

			TerminatorInst *tinst = (*it)->getTerminator();
			unsigned nsucc = tinst->getNumSuccessors();
			SwitchInst *SI1, *SI2;
			bool swcreated1 = false, swcreated2 = false; 
			for (unsigned i = 0; i < nsucc; i++) {
				BasicBlock *bsucc = tinst->getSuccessor(i);

				if (L->contains(bsucc)) { //successor is still inside loop?
					BasicBlock *destblock1;
					BasicBlock *destblock2 = realtodummy[bsucc].second;

					if (instnum[bsucc] < instnum[*it]) //points to earlier block
						destblock1 = realtodummy[bsucc].second;
					else //points to a latter block
						destblock1 = realtodummy[bsucc].first;

					if (!swcreated1) { //need to create switch instruction for top half
						SI1 = builder1.CreateSwitch(ConstantInt::get(
								Type::getInt64Ty(getGlobalContext()), 0),
						        destblock1, nsucc);
						swcreated1 = true;
					}

					//Add a case to the switch instruction for the top half
					SI1->addCase(ConstantInt::get(
							Type::getInt64Ty(getGlobalContext()), i),
							destblock1);

					//Now, deal with instruction for bottom half
					if (!swcreated2) { //need to create switch instr. for bottom half
						SI2 = builder2.CreateSwitch(ConstantInt::get(
								Type::getInt64Ty(getGlobalContext()), 0),
								destblock2, nsucc);
						swcreated2 = true;
					}

					//Add a case to the switch instruction for the bottom half
					SI2->addCase(ConstantInt::get(
							Type::getInt64Ty(getGlobalContext()), i),
							destblock2);

				}
			}
		} while (it != bb_ordered.begin());
	}

	cout<<">>Printing out blocks"<<endl;
	for (Function::iterator FI = ctrlfunc->begin(), FE = ctrlfunc->end();
		 FI != FE; ++FI) {
		cout<<"Contents of block "<<(*FI).getName().str()<<":"<<endl;
		(*FI).print(outstream);
		cout<<endl;
	}

	/*
	 *
	 * Begin control dependence calculation
	 *
	 */

	PostDominatorTree &pdt = Pass::getAnalysis<PostDominatorTree>(ctrlfuncref);

	for (std::vector<BasicBlock *>::iterator it = dummylist.begin();
		 it != dummylist.end(); ++it)
	{
		TerminatorInst *tinst = (*it)->getTerminator();
		unsigned nsucc = tinst->getNumSuccessors(); 

		for (unsigned i = 0; i < nsucc; i++) {
			BasicBlock *bsucc = tinst->getSuccessor(i);

			//Find LCA of two nodes in the post-dominator tree
			DomTreeNode *succnode = pdt.getNode(bsucc);
			BasicBlock *realblock = dummytoreal[*it];
			DomTreeNode *dn = pdt.getNode(pdt.findNearestCommonDominator(*it, bsucc));

			//WHAT IF dn IS NULL?

			//As long as the current node is not a post-dominator for *it, add a control
			//dependence edge and move upward in the post-dominator tree
			while (succnode != dn)
			{
				BasicBlock *depblock = succnode->getBlock();
				if (realblock != dummytoreal[depblock]) 
					addEdge(realblock->getTerminator(), depblock->begin(), CONTROL);

				succnode = succnode->getIDom();
			}
		}
	}
}
