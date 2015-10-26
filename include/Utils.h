#ifndef UTIL_H_
#define UTIL_H_

#include "llvm/Config/llvm-config.h"

#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7
#include "llvm/Instructions.h"
#else
#include "llvm/IR/Instructions.h"
#endif

#include <iostream>
#include <string>
#include <sstream>
#include <string>


using namespace llvm;
using namespace std;

string itoa(int i);
void error(const char * info);
void error(string info);

inline void replaceUses(PHINode *phi, map<Value*, Value*> repMap) {
    for (unsigned int j = 0, je = phi->getNumIncomingValues(); j < je; ++j) {
        Value *val = phi->getIncomingValue(j);
        if (Value *newArg = repMap[val]) {
            phi->setIncomingValue(j, newArg);
        }
    }
}
inline void replaceUses(User *user, map<Value*, Value*> repMap) {
    for (unsigned int j = 0, je = user->getNumOperands(); j < je; ++j) {
        Value *op = user->getOperand(j);
        if (Value *newArg = repMap[op]) {
            user->setOperand(j, newArg);
        }
    }
}

class Utils {
private:
	static int id;
public:
	static string genId();	//return a new identifier
};

#endif /* UTIL_H_ */