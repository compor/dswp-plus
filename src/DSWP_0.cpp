//include the public part of DSWP
#include "DSWP.h"

using namespace llvm;
using namespace std;

QNode::QNode(int u, int latency) {
	this->u = u;
	this->latency = latency;
}

bool QNode::operator < (const QNode &y) const {
	return latency < y.latency;
}

Edge::Edge(Instruction *u, Instruction *v, DType dtype) {
	this->u = u;
	this->v = v;
	this->dtype = dtype;
}

char DSWP::ID = 0;
RegisterPass<DSWP> X("dswp", "15745 Decoupled Software Pipeline");

DSWP::DSWP() : LoopPass (ID){
	loopCounter = 0;
}

bool DSWP::doInitialization(Loop *L, LPPassManager &LPM) {
	Module *mod = L->getHeader()->getParent()->getParent();

    cout << "#### processing module: " << mod->getName().data() << endl;

	Function *produce = mod->getFunction("sync_produce");
	if (produce == NULL) {	//the first time, we need to link them

		LLVMContext &ctx = mod->getContext();
		Type *void_ty = Type::getVoidTy(ctx),
		     *int32_ty = Type::getInt32Ty(ctx),
		     *int64_ty = Type::getInt64Ty(ctx),
		     *int8_ptr_ty = Type::getInt8PtrTy(ctx),
		     *elTy = int64_ty;

		//add sync_produce function
		vector<Type *> produce_arg;
		produce_arg.push_back(elTy);
		produce_arg.push_back(int32_ty);
		FunctionType *produce_ft = FunctionType::get(void_ty, produce_arg, false);
		produce = Function::Create(produce_ft, Function::ExternalLinkage, "sync_produce", mod);
		produce->setCallingConv(CallingConv::C);

		//add syn_consume function
		vector<Type *> consume_arg;
		consume_arg.push_back(int32_ty);
		FunctionType *consume_ft = FunctionType::get(elTy, consume_arg, false);
		Function *consume = Function::Create(consume_ft, Function::ExternalLinkage, "sync_consume", mod);
		consume->setCallingConv(CallingConv::C);

		//add sync_join
		FunctionType *join_ft = FunctionType::get(void_ty, false);
		Function *join = Function::Create(join_ft, Function::ExternalLinkage, "sync_join", mod);
		join->setCallingConv(CallingConv::C);

		//add sync_init
		FunctionType *init_ft = FunctionType::get(void_ty, false);
		Function *init = Function::Create(init_ft, Function::ExternalLinkage, "sync_init", mod);
		init->setCallingConv(CallingConv::C);

		//add sync_delegate
		vector<Type *>  argFunArg;
		argFunArg.push_back(int8_ptr_ty);
		FunctionType *argFun = FunctionType::get(int8_ptr_ty, argFunArg, false);

		vector<Type *> delegate_arg;
		delegate_arg.push_back(int32_ty);
		delegate_arg.push_back(PointerType::get(argFun, 0));
		delegate_arg.push_back(int8_ptr_ty);
		FunctionType *delegate_ft = FunctionType::get(void_ty, delegate_arg, false);
		Function *delegate = Function::Create(delegate_ft, Function::ExternalLinkage, "sync_delegate", mod);
		delegate->setCallingConv(CallingConv::C);

		//add show value
		vector<Type *> show_arg;
		show_arg.push_back(int64_ty);
		FunctionType *show_ft = FunctionType::get(void_ty, show_arg, false);
		Function *show = Function::Create(show_ft, Function::ExternalLinkage, "showValue", mod);
		show->setCallingConv(CallingConv::C);

		//add showPlace value
		vector<Type *> show2_arg;
		FunctionType *show2_ft = FunctionType::get(void_ty, show2_arg, false);
		Function *show2 = Function::Create(show2_ft, Function::ExternalLinkage, "showPlace", mod);
		show2->setCallingConv(CallingConv::C);

		//add showPtr value
		vector<Type *> show3_arg;
		show3_arg.push_back(int8_ptr_ty);
		FunctionType *show3_ft = FunctionType::get(void_ty, show3_arg, false);
		Function *show3 = Function::Create(show3_ft, Function::ExternalLinkage, "showPtr", mod);
		show3->setCallingConv(CallingConv::C);

		return true;
	}
	return false;
}

void DSWP::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<PostDominatorTree>();
    AU.addRequired<AliasAnalysis>();
    AU.addRequired<MemoryDependenceAnalysis>();
	AU.addRequiredTransitive<PostDominatorTree>();
}

bool DSWP::initialize(Loop *L) {
	// loop-level initialization. shouldn't do this in doInitialize because
	// it's not necessarily called immediately before runOnLoop....
	header = L->getHeader();
	func = header->getParent();
	m_module = func->getParent();
	context = &m_module->getContext();
	eleType = Type::getInt64Ty(*context);

	predecessor = L->getLoopPredecessor();
	exit = L->getExitBlock();
	loopCounter++;

	if (exit == NULL) {
		cerr << "loop exit not unique" << endl;
		return true;
	} else if (predecessor == NULL) {
		cerr << "loop predecessor not unique" << endl;
		return true;
	}
	return false;
}


bool DSWP::runOnLoop(Loop *L, LPPassManager &LPM) {
	if (L->getLoopDepth() != 1)	//ONLY care about top level loops
    	return false;

	if (generated.find(L->getHeader()->getParent()) != generated.end())	//this is the generated code
		return false;

	cout << "///////////////////////////// we are running on a loop" << endl;

	bool bad = initialize(L);
	if (bad) {
		clear();
		return false;
	}

	buildPDG(L);
	showGraph(L);
	findSCC(L);

	if (sccNum == 1) {
		cout << "only one SCC, can't do nuttin" << endl;
		clear();
		return false;
	}

	showDAG(L);
	threadPartition(L);
	showPartition(L);
	getLiveinfo(L);
	showLiveInfo(L);
	getDominators(L);
	// TODO: should estimate whether splitting was helpful and if not, return
	//       the unmodified code (like in the paper)
	preLoopSplit(L);
	loopSplit(L);
	insertSynchronization(L);
	cout << "#######################################" << endl;
	cleanup(L, LPM);
	clear();
	cout << "//////////////////////////// we finsih run on a loop " << endl;
	return true;
}


void DSWP::addEdge(Instruction *u, Instruction *v, DType dtype) {
	if (std::find(pdg[u].begin(), pdg[u].end(), Edge(u, v, dtype)) ==
				pdg[u].end()) {
		pdg[u].push_back(Edge(u, v, dtype));
		allEdges.push_back(Edge(u, v, dtype));
		rev[v].push_back(Edge(v, u, dtype));
	}
	else
		cout<<">>Skipping the edge, as it has been added already."<<endl;
}

bool DSWP::checkEdge(Instruction *u, Instruction *v) {

	for (vector<Edge>::iterator it = pdg[u].begin(); it != pdg[v].end(); it++) {
		if (it->v == v) {
			//TODO report something
			return true;
		}
	}
	return false;
}
