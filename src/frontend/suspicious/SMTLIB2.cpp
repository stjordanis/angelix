#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <fstream>

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/Support/Format.h"

#include "SMTLIB2.h"


using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;


bool replaceStr(std::string& str, const std::string& from, const std::string& to) {
  size_t start_pos = str.find(from);
  if(start_pos == std::string::npos)
    return false;
  str.replace(start_pos, from.length(), to);
  return true;
}


class StmtToSMTLIB2 : public StmtVisitor<StmtToSMTLIB2> {
  raw_ostream &OS;
  clang::PrinterHelper* Helper;
  PrintingPolicy Policy;
  
public:
  StmtToSMTLIB2(raw_ostream &os, PrinterHelper* helper, const PrintingPolicy &Policy):
    OS(os), Helper(helper), Policy(Policy) {}

  void PrintExpr(Expr *E) {
    if (E)
      Visit(E);
    else
      OS << "<null expr>";
  }

  void Visit(Stmt* S) {
    if (Helper && Helper->handledStmt(S, OS))
      return;
    else StmtVisitor<StmtToSMTLIB2>::Visit(S);
  }

  void VisitBinaryOperator(BinaryOperator *Node) {  
    std::string opcode_str = BinaryOperator::getOpcodeStr(Node->getOpcode()).lower();
    if (opcode_str == "!=") {
      OS << "(not (= ";
      PrintExpr(Node->getLHS());
      OS << " ";
      PrintExpr(Node->getRHS());
      OS << "))";
      return;
    }

    replaceStr(opcode_str, "==", "=");
    replaceStr(opcode_str, "||", "or");
    replaceStr(opcode_str, "&&", "and");
  
    OS << "(" << opcode_str << " ";
    PrintExpr(Node->getLHS());
    OS << " ";
    PrintExpr(Node->getRHS());
    OS << ")";
  }

  void VisitUnaryOperator(UnaryOperator *Node) {
    std::string op = UnaryOperator::getOpcodeStr(Node->getOpcode());
    if (op == "!") {
      op = "not";
    }

    OS << "(" << op << " ";
    PrintExpr(Node->getSubExpr());
    OS << ")";
  }

  void VisitImplicitCastExpr(ImplicitCastExpr *Node) {
    PrintExpr(Node->getSubExpr());
  }

  void VisitIntegerLiteral(IntegerLiteral *Node) {
    bool isSigned = Node->getType()->isSignedIntegerType();
    OS << Node->getValue().toString(10, isSigned);
  }

  void VisitCharacterLiteral(CharacterLiteral *Node) {
    unsigned value = Node->getValue();

    //TODO: it most likely does not produce valid SMTLIB2:
    switch (value) {
    case '\\':
      OS << "'\\\\'";
      break;
    case '\'':
      OS << "'\\''";
      break;
    case '\a':
      // TODO: K&R: the meaning of '\\a' is different in traditional C
      OS << "'\\a'";
      break;
    case '\b':
      OS << "'\\b'";
      break;
      // Nonstandard escape sequence.
      /*case '\e':
        OS << "'\\e'";
        break;*/
    case '\f':
      OS << "'\\f'";
      break;
    case '\n':
      OS << "'\\n'";
      break;
    case '\r':
      OS << "'\\r'";
      break;
    case '\t':
      OS << "'\\t'";
      break;
    case '\v':
      OS << "'\\v'";
      break;
    default:
      if (value < 256 && isPrintable((unsigned char)value))
        OS << "'" << (char)value << "'";
      else if (value < 256)
        OS << "'\\x" << llvm::format("%02x", value) << "'";
      else if (value <= 0xFFFF)
        OS << "'\\u" << llvm::format("%04x", value) << "'";
      else
        OS << "'\\U" << llvm::format("%08x", value) << "'";
    }
  }

  void VisitDeclRefExpr(DeclRefExpr *Node) {
    OS << Node->getNameInfo();
  }

};

std::string toSMTLIB2(const Stmt* stmt) {
    std::string str;
    llvm::raw_string_ostream rso(str);
    PrintingPolicy pp = PrintingPolicy(LangOptions());
    StmtToSMTLIB2 T(rso, nullptr, pp);
    T.Visit(const_cast<Stmt*>(stmt));
    std::string stmtStr = rso.str();
    return stmtStr;
}