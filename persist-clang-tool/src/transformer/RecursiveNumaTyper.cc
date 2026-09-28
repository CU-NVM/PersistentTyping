/**
 * @file RecursiveNumaTyper.cc
 * @brief Implementation of NUMA-aware class transformation and specialization
 * @author Kidus Workneh
 * 
 * This file implements the RecursiveNumaTyper class which automatically transforms
 * C++ classes to be NUMA-aware by creating specialized versions that allocate
 * memory on specific NUMA nodes for improved performance in multi-socket systems.
 */

#include "RecursiveNumaTyper.h"
#include "../numafy/new_allocs.h"
#include "../utils/utils.h"
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <string>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include <sstream>
#include <string>
#include <unordered_map>
#include <map>
#include <unordered_set>

// Template string for NUMA allocator functions with type conversion operators
std::string allocator_funcs = R"(using allocator_type = NumaAllocator<T,NodeID>;// Alloc<T, NodeID>;
using pointer_alloc_type =NumaAllocator<T*,NodeID>; //Alloc<T*, NodeID>;
public:
       
    numa(T t) : T(t) {}
    inline operator T() {return *this;}
    inline operator T&(){return *this;}
	inline T operator-> (){
		static_assert(std::is_pointer<T>::value,"-> operator is only valid for pointer types");
		return this;
	}

    static void* operator new(std::size_t count)
    {
        allocator_type alloc;
        return alloc.allocate(count);
    }
 
    static void* operator new[](std::size_t count)
    {
        //todo: disable placement new for numa.
        allocator_type alloc;
        return alloc.allocate(count);
    }
	
    numa& operator[](std::size_t index) {
        static_assert(std::is_pointer<T>::value,"[] operator is only valid for pointer types");
        return this[index];
    }

    // Const version of the [] operator
    const T& operator[](std::size_t index) const {
        static_assert(std::is_pointer<T>::value,"[] operator is only valid for pointer types");
        return this[index];
    }

    int node_id = NodeID;
    constexpr operator int() const { return NodeID; }
    )";

/**
 * @brief Generates NUMA-aware memory allocation code for classes
 * @param classDecl Name of the class being specialized  
 * @param nodeID Target NUMA node ID for allocation
 * @return String containing custom new/delete operators with NUMA allocation
 */
std::string utils::getNumaAllocatorCode(std::string classDecl, std::string nodeID){
 return R"(public: 
    static void* operator new(std::size_t sz){
        void* p;
        #ifdef UMF
            p= umf_alloc()"+ nodeID + R"( ,sizeof()"+classDecl +R"(),alignof()"+ classDecl + R"());
        #else
            p = numa_alloc_onnode(sz* sizeof()"+classDecl+R"(), )"+ nodeID +R"();
        #endif
        
        if (p == nullptr) {
            std::cout<<"allocation failed\n";
            throw std::bad_alloc();
        }
        return p;
    }

    static void* operator new[](std::size_t sz){
        void* p;
        #ifdef UMF
            p= umf_alloc()"+ nodeID + R"( ,sizeof()"+classDecl +R"(),alignof()"+ classDecl + R"());
        #else
            p = numa_alloc_onnode(sz* sizeof()"+classDecl+R"(), )"+ nodeID +R"();
        #endif
        
        if (p == nullptr) {
            std::cout<<"allocation failed\n";
            throw std::bad_alloc();
        }
        return p;
    }

    static void operator delete(void* ptr){
        // cout<<"doing numa free \n";
        #ifdef UMF
			umf_free()"+ nodeID +R"(,ptr);
		#else
		    numa_free(ptr, 1 * sizeof()"+classDecl+R"());
        #endif
    }

    static void operator delete[](void* ptr){
		// cout<<"doing numa free \n";
        #ifdef UMF
			umf_free()"+ nodeID +R"(,ptr);
		#else
		    numa_free(ptr, 1 * sizeof()"+classDecl+R"());
        #endif
    }
)";
}

RecursiveNumaTyper::RecursiveNumaTyper(clang::ASTContext &context, clang::Rewriter &rewriter)
    : Transformer(context, rewriter)
{}

using namespace clang;

/**
 * @brief Main entry point for NUMA transformation process
 * 
 * Scans the AST for NUMA heap instantiations using 'new' operators and variable declarations,
 * then generates specialized classes for each unique type-node combination.
 */
void RecursiveNumaTyper::start()
{
    using namespace clang::ast_matchers;
    MatchFinder newFinder;
    MatchFinder varFinder;
    
    // Match new expressions with NUMA types, excluding system headers and NUMA library files
    auto newExprMatcher = cxxNewExpr(unless(isExpansionInSystemHeader()),
                                    unless(isExpansionInFileMatching("numatype.hpp")),
                                    unless(isExpansionInFileMatching("numathreads.hpp")),
                                    unless(isExpansionInFileMatching(".*/umf/.*")),
                                    hasType(pointsTo(qualType(hasDeclaration(namedDecl(matchesName("^::?numa"))))))).bind("newExpr");
    
    // Match variable declarations with NUMA types
    auto varDeclMatcher = varDecl(unless(isExpansionInSystemHeader()),
                                    unless(isExpansionInFileMatching("numatype.hpp")),
                                    unless(isExpansionInFileMatching("numathreads.hpp")),
                                    unless(isExpansionInFileMatching(".*/umf/.*")),
                                    hasType(pointsTo(qualType(hasDeclaration(namedDecl(matchesName("^::?numa"))))))).bind("varDecl");
    
    newFinder.addMatcher(newExprMatcher, this);
    newFinder.matchAST(context);
    varFinder.addMatcher(varDeclMatcher, this);
    varFinder.matchAST(context);

    addAllSpecializations(context);
    
    // Process new expressions and create specializations
    for(auto newExpr : newNumaExprs){
        CXXRecordDecl* FirstTempArg;
        llvm::APInt SecondTempArg;
        auto CXXRecordNumaType = newExpr->getType()->getPointeeType()->getAsCXXRecordDecl();
        auto TemplateNumaType = dyn_cast<ClassTemplateSpecializationDecl>(CXXRecordNumaType);
        llvm::ArrayRef<TemplateArgument> TemplateArgs = TemplateNumaType->getTemplateArgs().asArray();
        if(const RecordType* RT = TemplateArgs[0].getAsType()->getAs<RecordType>()){
            if (CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RT->getDecl())) {
                FirstTempArg = CXXRD;
                SecondTempArg = TemplateArgs[1].getAsIntegral();
                specializeClass(context,FirstTempArg,SecondTempArg);
            }
        }
    }

    // Process variable declarations and create specializations
    for(auto varDecl : numaVarDecls){
        CXXRecordDecl* FirstTempArg;
        llvm::APInt SecondTempArg;
        auto CXXRecordNumaType = varDecl->getType()->getPointeeType()->getAsCXXRecordDecl();
        auto TemplateNumaType = dyn_cast<ClassTemplateSpecializationDecl>(CXXRecordNumaType);
        llvm::ArrayRef<TemplateArgument> TemplateArgs = TemplateNumaType->getTemplateArgs().asArray();
        if(const RecordType* RT = TemplateArgs[0].getAsType()->getAs<RecordType>()){
            if (CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RT->getDecl())) {
                FirstTempArg = CXXRD;
                SecondTempArg = TemplateArgs[1].getAsIntegral();
                specializeClass(context,FirstTempArg,SecondTempArg);
            }
        }
    }
    //print(llvm::outs());
    return;
}

/**
 * @brief Generates NUMA constructor signature with parameter list
 * @param constructor Original constructor declaration
 * @return String representation of NUMA constructor signature
 */
std::string RecursiveNumaTyper::getNumaConstructorSignature(clang::CXXConstructorDecl* constructor) {
    std::string ConstructorName = constructor->getParent()->getNameAsString();    
    FunctionDecl* constructorDefinition= constructor->getDefinition();
    
    std::string ConstructorSignature = "numa (";
    unsigned ParamCount = constructorDefinition->getNumParams();
    
    // Build parameter list with types and names
    for (unsigned i = 0; i < ParamCount; ++i) {
        ParmVarDecl *Param = constructorDefinition->getParamDecl(i);
        
        std::string ParamType;
        llvm::raw_string_ostream OS(ParamType);
        Param->getType().print(OS, constructorDefinition->getASTContext().getPrintingPolicy());
        
        ConstructorSignature += OS.str();
        
        if (!Param->getName().empty()) {
            ConstructorSignature += " " + Param->getNameAsString();
        }
        
        if (i < ParamCount - 1) {
            ConstructorSignature += ", ";
        }
    }
    ConstructorSignature += ")";

    return ConstructorSignature;
}

/**
 * @brief Builds member initializer list string for constructors
 * @param initMemberlist Map of member names to initialization values
 * @return Formatted member initializer list string
 */
std::string utils::getMemberInitString(std::map<std::string, std::string>& initMemberlist) {
    std::string result = ": ";
    for (auto it = initMemberlist.begin(); it != initMemberlist.end(); ++it) {
    result += it->first + "(" + it->second + ")";
    
        if (std::next(it) != initMemberlist.end()) {
            result += ", ";
        }
    }
    return result;
}

/**
 * @brief Extracts delegating constructor initialization string
 * @param constructor Constructor with delegating initializer
 * @return Formatted delegating constructor call string
 */
std::string utils::getDelegatingInitString(CXXConstructorDecl* constructor) {
     for (const auto *Init : constructor->inits()) {
        if (Init->isDelegatingInitializer()) {
            if (const auto *ConstructExpr = dyn_cast<CXXConstructExpr>(Init->getInit())) {
                unsigned ArgCount = ConstructExpr->getNumArgs();
                std::string DelegatedSignature = ": numa(";
                
                for (unsigned i = 0; i < ArgCount; ++i) {
                    const Expr *ArgExpr = ConstructExpr->getArg(i);
                    std::string ArgStr;
                    llvm::raw_string_ostream ArgOS(ArgStr);
                    ArgExpr->printPretty(ArgOS, nullptr, constructor->getASTContext().getPrintingPolicy());
                    
                    DelegatedSignature += ArgOS.str();
                    if (i < ArgCount - 1) {
                        DelegatedSignature += ", ";
                    }
                }
                DelegatedSignature += ")";
                return DelegatedSignature;
            }
        }
    }
    return " ";
}

/**
 * @brief Generates NUMA method signature with return type and parameters and makes the method virtual
 * @param method Original method declaration
 * @return Complete method signature string with virtual keyword
 */
std::string RecursiveNumaTyper::getNumaMethodSignature(CXXMethodDecl* method){
    FunctionDecl *methodDefinition = method->getDefinition();
    std::string ReturnTypeStr;
    llvm::raw_string_ostream ReturnTypeOS(ReturnTypeStr);
    methodDefinition->getReturnType().print(ReturnTypeOS, methodDefinition->getASTContext().getPrintingPolicy());

    std::string MethodName = methodDefinition->getNameAsString();

    // Build parameter list
    std::string ParamsStr = "(";
    for (unsigned i = 0; i < methodDefinition->getNumParams(); ++i) {
        ParmVarDecl *Param = methodDefinition->getParamDecl(i);
        std::string ParamTypeStr;
        llvm::raw_string_ostream ParamTypeOS(ParamTypeStr);
        Param->getType().print(ParamTypeOS, methodDefinition->getASTContext().getPrintingPolicy());

        ParamsStr += ParamTypeOS.str();
        if (!Param->getName().empty()) {
            ParamsStr += " " + Param->getNameAsString();
        }

        if (i < method->getNumParams() - 1) {
            ParamsStr += ", ";
        }
    }
    ParamsStr += ")";

    //Full method signature with virtual keyword
    std::string MethodSignature = "virtual "+ReturnTypeOS.str() + " " + MethodName + ParamsStr;
    return MethodSignature;
}

/**
 * @brief Discovers all NUMA template specializations in the translation unit
 * @param Context AST context for traversing declarations
 */
void RecursiveNumaTyper::addAllSpecializations(const clang::ASTContext& Context){
    clang::TranslationUnitDecl *TU = Context.getTranslationUnitDecl();
    for (const auto *Decl : TU->decls()) {
        if (const auto *SpecDecl = clang::dyn_cast<clang::ClassTemplateSpecializationDecl>(Decl)) {
            const auto *TemplateDecl = SpecDecl->getSpecializedTemplate();
            
            // Check if it's a numa template specialization
            if(TemplateDecl->getNameAsString().compare("numa") == 0){
                clang::CXXRecordDecl* FoundType;
                llvm::APSInt FoundInt;
                
                // Extract template arguments (type and integral node ID)
                for (const auto &Arg : SpecDecl->getTemplateArgs().asArray()) {
                    if (Arg.getKind() == clang::TemplateArgument::ArgKind::Type) {
                        if(const RecordType* RT = Arg.getAsType()->getAs<RecordType>()){
                            if (CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RT->getDecl())) {
                                FoundType= CXXRD;
                            }
                        }
                    }
                    if (Arg.getKind() == clang::TemplateArgument::ArgKind::Integral) {
                        FoundInt = Arg.getAsIntegral();
                        if(!NumaSpeclExists(FoundType, FoundInt)){
                            RecursiveNumaTyper::specializedClasses.push_back({FoundType, FoundInt});
                        }
                    }
                }
            }
        }
    }   
}

/**
 * @brief Checks if a NUMA specialization already exists
 * @param FirstTempArg Class declaration to check
 * @param SecondTempArg NUMA node ID
 * @return true if specialization exists, false otherwise
 */
bool RecursiveNumaTyper::NumaSpeclExists(const clang::CXXRecordDecl* FirstTempArg, llvm::APInt SecondTempArg){
     std::pair<const clang::CXXRecordDecl*,llvm::APInt> searchPair = {FirstTempArg, SecondTempArg};

    auto it = std::find(specializedClasses.begin(), specializedClasses.end(), searchPair);

    if (it != specializedClasses.end()) {
        return true;
    } else {
        return false;
    }
}

/**
 * @brief Makes all class methods virtual to enable NUMA polymorphism
 * @param classDecl Class declaration to modify
 */
void RecursiveNumaTyper::makeVirtual(const CXXRecordDecl* classDecl){
    for(auto method : classDecl->methods()){
        if(method->isUserProvided()){
            if(method->isVirtual()){
                continue;
            }
            // Skip constructors when adding virtual keyword
            if(method->getNameAsString() != classDecl->getNameAsString()){
                method->setVirtualAsWritten(true);
            }
            
            if(method->getNameAsString() != classDecl->getNameAsString()){
                rewriteLocation = method->getBeginLoc();
                rewriter.InsertTextBefore(rewriteLocation, "virtual ");
                fileIDs.push_back(rewriter.getSourceMgr().getFileID(rewriteLocation));     
            }
        }
    }
}

/**
 * @brief Creates a new NUMA specialization for a class if it doesn't exist
 * @param Context AST context
 * @param FirstTempArg Class to specialize
 * @param SecondTempArg Target NUMA node ID
 */
void RecursiveNumaTyper::specializeClass(const clang::ASTContext& Context, const clang::CXXRecordDecl* FirstTempArg, llvm::APInt SecondTempArg){
    if(NumaSpeclExists(FirstTempArg, SecondTempArg)){
        return;
    }
    specializedClasses.push_back({FirstTempArg, SecondTempArg});
    constructSpecialization(Context, FirstTempArg, SecondTempArg);
}

/**
 * @brief Constructs complete NUMA class specialization with all members
 * @param Context AST context
 * @param classDecl Original class declaration
 * @param nodeID Target NUMA node ID
 */
void RecursiveNumaTyper::constructSpecialization(const clang::ASTContext& Context,const clang::CXXRecordDecl* classDecl, llvm::APInt nodeID){
    makeVirtual(classDecl);
    uint64_t nodeIDVal = nodeID.getLimitedValue();
    rewriteLocation = classDecl->getEndLoc();
    SourceLocation semiLoc = Lexer::findLocationAfterToken(
                rewriteLocation, tok::semi, rewriter.getSourceMgr(), rewriter.getLangOpts(), 
                /*SkipTrailingWhitespaceAndNewLine=*/true);
    
    // Categorize class members by access level
    std::vector<FieldDecl*> publicFields;
    std::vector<FieldDecl*> privateFields;
    std::vector<CXXMethodDecl*> publicMethods;
    std::vector<CXXMethodDecl*> privateMethods;

    for(auto field : classDecl->fields()){
        if(field->getAccess() == AS_public){
            publicFields.push_back(field);
        }
        else if(field->getAccess() == AS_private){
            privateFields.push_back(field);
        }
    }

    for(auto method : classDecl->methods()){
        if(method->getAccess() == AS_public){
            publicMethods.push_back(method);
        }
        else if(method->getAccess() == AS_private){
            privateMethods.push_back(method);
        }
    }

    // Generate template specialization class
    rewriter.InsertTextAfter(semiLoc, "\ntemplate<>\n"
                                            "class numa<"+classDecl->getNameAsString()+"," + std::to_string(nodeIDVal)+">{\n");
    rewriter.InsertTextAfter(semiLoc, utils::getNumaAllocatorCode(classDecl->getNameAsString(), std::to_string(nodeIDVal)));

    numaPublicMembers(Context, semiLoc, publicFields, publicMethods, nodeID);
    numaPrivateMembers(Context, semiLoc, privateFields, privateMethods, nodeID);
    rewriter.InsertTextAfter(semiLoc, "};\n");  

    fileIDs.push_back(rewriter.getSourceMgr().getFileID(rewriteLocation));
}

/**
 * @brief Generates NUMA-aware versions of public class members
 * @param Context AST context
 * @param rewriteLocation Location to insert generated code
 * @param publicFields Public field declarations
 * @param publicMethods Public method declarations  
 * @param nodeID Target NUMA node ID
 */
void RecursiveNumaTyper::numaPublicMembers(const clang::ASTContext& Context, clang::SourceLocation& rewriteLocation, std::vector<FieldDecl*> publicFields, std::vector<CXXMethodDecl*> publicMethods, llvm::APInt nodeID){
    rewriter.InsertTextAfter(rewriteLocation, "public:\n");
     for(auto fields :publicFields){
        uint64_t nodeIDVal = nodeID.getLimitedValue();

        /*Case where the field is builtin type*/
        if(fields->getType()->isBuiltinType()){
            rewriter.InsertTextAfter(rewriteLocation, "numa<"+fields->getType().getAsString()+","+ std::to_string(nodeIDVal) +"> "+ fields->getNameAsString()+";\n" );
        }

        /*Case where the field is a built in type and a pointer*/
        else if(fields->getType()->isPointerType() && fields->getType()->getPointeeType()->isBuiltinType()){
            rewriter.InsertTextAfter(rewriteLocation, "numa<"+fields->getType()->getPointeeType().getAsString() +"*,"+std::to_string(nodeIDVal)+"> "+ fields->getNameAsString()+";\n" );
        }

        /*Case where the field is not a built in type but is a pointer*/
        else if(fields->getType()->isPointerType() && !fields->getType()->getPointeeType()->isBuiltinType()){
            rewriter.InsertTextAfter(rewriteLocation, "numa<"+fields->getType()->getPointeeType().getAsString() +"*,"+std::to_string(nodeIDVal)+"> "+ fields->getNameAsString()+";\n" );

            // Get the ultimate pointee type by dereferencing all pointer levels
            clang::QualType pointeeType;
            clang::QualType fieldType = fields->getType();
    
            while(const auto *PT = dyn_cast<clang::PointerType>(fieldType.getTypePtr())){
                pointeeType = PT->getPointeeType();
                fieldType = pointeeType;
            }
            
            if(NumaSpeclExists(pointeeType->getAsCXXRecordDecl() , nodeID)){
                continue;
            }
            else{
                llvm::outs() << "Specializing for pointer to non-builtin type: " << pointeeType->getAsCXXRecordDecl()->getNameAsString() << "\n";
                RecursiveNumaTyper::specializedClasses.push_back({pointeeType->getAsCXXRecordDecl(), nodeID});
                constructSpecialization(Context, pointeeType->getAsCXXRecordDecl(), nodeID);
            }
        }
        /*Case where the field is not a built in type and not a pointer*/
        else if (!fields->getType()->isBuiltinType() && !fields->getType()->isPointerType()){
            rewriter.InsertTextAfter(rewriteLocation, "numa<"+fields->getType().getAsString() +","+std::to_string(nodeIDVal)+"> "+ fields->getNameAsString()+";\n" );
            if(NumaSpeclExists(fields->getType()->getAsCXXRecordDecl(), nodeID)){
                continue;
            }else{
                RecursiveNumaTyper::specializedClasses.push_back({fields->getType()->getAsCXXRecordDecl(), nodeID});
                constructSpecialization(Context, fields->getType()->getAsCXXRecordDecl(), nodeID);
            }
        }
    }   

    // Generate NUMA versions of public methods
    for(auto method : publicMethods){
        if (auto Ctor = dyn_cast<CXXConstructorDecl>(method)){   
            if(Ctor->isUserProvided()){
                numaConstructors(Ctor, rewriteLocation, nodeID);
            }
        }
        else if (auto Dtor = dyn_cast<CXXDestructorDecl>(method)){
            if(Dtor->isUserProvided()){
                numaDestructors(Dtor, rewriteLocation, nodeID);
            }
        }
        else{
            if(method->getDefinition()){
                if(method->isUserProvided()){
                    numaMethods(Context, method,rewriteLocation,nodeID);
                }
            }
        }
    }
}

/**
 * @brief Generates NUMA-aware versions of private class members
 * @param Context AST context
 * @param rewriteLocation Location to insert generated code
 * @param privateFields Private field declarations
 * @param privateMethods Private method declarations
 * @param nodeID Target NUMA node ID
 */
void RecursiveNumaTyper::numaPrivateMembers(const clang::ASTContext& Context, clang::SourceLocation& rewriteLocation, std::vector<FieldDecl*> privateFields, std::vector<CXXMethodDecl*> privateMethods, llvm::APInt nodeID){
    rewriter.InsertTextAfter(rewriteLocation, "private:\n");
    for(auto fields :privateFields){
        uint64_t nodeIDVal = nodeID.getLimitedValue();
        /*Case where the field is a built in type but not a pointer */
        if(fields->getType()->isBuiltinType()){
            rewriter.InsertTextAfter(rewriteLocation, "numa<"+fields->getType().getAsString()+","+std::to_string(nodeIDVal )+"> "+ fields->getNameAsString()+";\n" );
        }

        /*Case where the field is a built in type and a pointer*/
        else if(fields->getType()->isPointerType() && fields->getType()->getPointeeType()->isBuiltinType()){
                rewriter.InsertTextAfter(rewriteLocation, "numa<"+fields->getType()->getPointeeType().getAsString() +"*,"+std::to_string(nodeIDVal)+"> "+ fields->getNameAsString()+";\n" );
        }

        /*Case where the field is not a built in type but is a pointer*/
        else if(fields->getType()->isPointerType() && !fields->getType()->getPointeeType()->isBuiltinType()){
            rewriter.InsertTextAfter(rewriteLocation, "numa<"+fields->getType()->getPointeeType().getAsString() +"*,"+std::to_string(nodeIDVal)+"> "+ fields->getNameAsString()+";\n" );
            clang::QualType pointeeType;
            clang::QualType fieldType = fields->getType();
            // Get the ultimate pointee type by dereferencing all pointer levels
            while(const auto *PT = dyn_cast<clang::PointerType>(fieldType.getTypePtr())){
                pointeeType = PT->getPointeeType();
                fieldType = pointeeType;
            }

            if(NumaSpeclExists(pointeeType->getAsCXXRecordDecl(), nodeID)){
                continue;
            }
            else{
                RecursiveNumaTyper::specializedClasses.push_back({pointeeType->getAsCXXRecordDecl(), nodeID});
                constructSpecialization(Context, pointeeType->getAsCXXRecordDecl(), nodeID);
            }
        }
        /*Case where the field is not a built in type and not a pointer*/
        else if (!fields->getType()->isBuiltinType() && !fields->getType()->isPointerType()){
            rewriter.InsertTextAfter(rewriteLocation, "numa<"+fields->getType().getAsString() +","+std::to_string(nodeIDVal)+"> "+ fields->getNameAsString()+";\n" );
            if(NumaSpeclExists(fields->getType()->getAsCXXRecordDecl(), nodeID)){
                continue;
            }else{
                RecursiveNumaTyper::specializedClasses.push_back({fields->getType()->getAsCXXRecordDecl(), nodeID});
                constructSpecialization(Context, fields->getType()->getAsCXXRecordDecl(), nodeID);
            }
        }
    } 
   
   // Generate NUMA versions of private methods
   for(auto method : privateMethods){
        if (auto Ctor = dyn_cast<CXXConstructorDecl>(method)){
            if(Ctor->isUserProvided()){
                numaConstructors(Ctor, rewriteLocation, nodeID);
            }
        }
        else if (auto Dtor = dyn_cast<CXXDestructorDecl>(method)){
            if(Dtor->isUserProvided()){
                   numaDestructors(Dtor, rewriteLocation, nodeID);
            }
        }
        else{
            if(method->getDefinition()){
               if(method->isUserProvided()){
                    numaMethods(Context, method,rewriteLocation,nodeID);
                }
            }
        }
    }
}

/**
 * @brief Creates NUMA-aware constructor with proper initialization
 * @param constructor Original constructor declaration
 * @param rewriteLocation Location to insert generated code
 * @param nodeID Target NUMA node ID
 */
void RecursiveNumaTyper::numaConstructors(clang::CXXConstructorDecl* constructor, clang::SourceLocation& rewriteLocation, llvm::APInt nodeID){
    std::map<std::string, std::string> initMembers;
    bool isDelegatingInit = false;
    bool isMemberInit = false;

    std::string initMembersString;
    std::string numaConstructorSignatrue = getNumaConstructorSignature(constructor);  
    
    // Handle constructor initializer lists
    if(constructor->getNumCtorInitializers() > 0){
        for (auto Init = constructor->init_begin(); Init != constructor->init_end(); ++Init) {
            if ((*Init)->isWritten()) {
                if((*Init)->isDelegatingInitializer()){
                    isDelegatingInit = true;
                }
                if((*Init)->isMemberInitializer()){
                    isMemberInit = true;
                    FieldDecl *initializedField = (*Init)->getMember();
                    if(initializedField){
                        std::string fieldName = initializedField->getNameAsString();
                        Expr *initExpr = (*Init)->getInit();
                        if (initExpr) {
                            std::string InitValue;
                            llvm::raw_string_ostream OS(InitValue);
                            initMembers[fieldName] = OS.str();
                        }
                    }
                    initMembersString= utils::getMemberInitString(initMembers);
                }
            }
        }
    }
    
    if (isMemberInit){
        numaConstructorSignatrue += initMembersString;
    }
    if(isDelegatingInit){
        numaConstructorSignatrue += utils::getDelegatingInitString(constructor);
    }
    rewriter.InsertTextAfter(rewriteLocation, numaConstructorSignatrue);

    if (constructor->hasBody()) { 
        const Stmt *ConstructorBody = constructor->getBody(); 
        std::string BodyStr;
        llvm::raw_string_ostream OS(BodyStr);
        ConstructorBody->printPretty(OS, nullptr, constructor->getASTContext().getPrintingPolicy());
        rewriter.InsertTextAfter(rewriteLocation, OS.str());
    }
    else{
        rewriter.InsertTextAfter(rewriteLocation, "{}\n");
    }
}

/**
 * @brief Creates NUMA-aware destructor
 * @param destructor Original destructor declaration
 * @param rewriteLocation Location to insert generated code  
 * @param nodeID Target NUMA node ID
 */
void RecursiveNumaTyper::numaDestructors(clang::CXXDestructorDecl* destructor, clang::SourceLocation& rewriteLocation, llvm::APInt nodeID){
    rewriter.InsertTextAfter(rewriteLocation, "virtual ~numa(");
    if (destructor->parameters().size() == 0){
        rewriter.InsertTextAfter(rewriteLocation, ")\n");
    
        if(destructor->hasBody()){
            SourceRange BodyRange = destructor->getBody()->getSourceRange();
            const SourceManager &SM = destructor->getASTContext().getSourceManager();
            llvm::StringRef BodyText = Lexer::getSourceText(CharSourceRange::getTokenRange(BodyRange), SM, destructor->getASTContext().getLangOpts());
            rewriter.InsertTextAfter(rewriteLocation, BodyText);
            rewriter.InsertTextAfter(rewriteLocation, "\n");
        }
    }
    else{
        // Handle destructors with parameters (rare case)
        for(auto param : destructor->getDefinition()->parameters())
        {
            rewriter.InsertTextAfter(rewriteLocation, param->getType().getAsString() + " " + param->getNameAsString());
            if(param != destructor->getDefinition()->parameters().back())
            {
                rewriter.InsertTextAfter(rewriteLocation, ", ");
            }
        }
        rewriter.InsertTextAfter(rewriteLocation, ")");

        if(destructor->hasBody()){
            SourceRange BodyRange = destructor->getBody()->getSourceRange();
            const SourceManager &SM = destructor->getASTContext().getSourceManager();
            llvm::StringRef BodyText = Lexer::getSourceText(CharSourceRange::getTokenRange(BodyRange), SM, destructor->getASTContext().getLangOpts());
            rewriter.InsertTextAfter(rewriteLocation, BodyText);
            rewriter.InsertTextAfter(rewriteLocation, "\n");
        }
    } 
}

/**
 * @brief Creates NUMA-aware method and handles new expressions in method body
 * @param Context AST context
 * @param method Original method declaration
 * @param rewriteLocation Location to insert generated code
 * @param nodeID Target NUMA node ID
 */
void RecursiveNumaTyper::numaMethods(const clang::ASTContext& Context, clang::CXXMethodDecl* method, clang::SourceLocation& rewriteLocation, llvm::APInt nodeID){
    std::string methodSignature = getNumaMethodSignature(method);
    rewriter.InsertTextAfter(rewriteLocation, methodSignature);
    
    if (method->hasBody()) {
        const Stmt *MethodBody = method->getBody();
        
        // Find new expressions in method body and create specializations
        std::vector<CXXNewExpr*> newExprsInBody = getNewExprFromMethods(Context, method);
        for(auto newExpr : newExprsInBody){
            CXXRecordDecl* FirstTempArg = newExpr->getType()->getPointeeType()->getAsCXXRecordDecl();
            llvm::APInt SecondTempArg = nodeID;
            specializeClass(Context,FirstTempArg,SecondTempArg);
        }
        
        std::string BodyStr;
        llvm::raw_string_ostream OS(BodyStr);
        MethodBody->printPretty(OS, nullptr, method->getASTContext().getPrintingPolicy());
        rewriter.InsertTextAfter(rewriteLocation, OS.str());
    }
    else{
        rewriter.InsertTextAfter(rewriteLocation, "{}\n");
    }
}

/**
 * @brief Processes AST matcher results for NUMA expressions and variable declarations
 * @param result Match result containing found NUMA expressions or declarations
 */
void RecursiveNumaTyper::run(const clang::ast_matchers::MatchFinder::MatchResult &result){
    if (const auto *E = result.Nodes.getNodeAs<CXXNewExpr>("newExpr")) {
            if(seenNewNumaTypes.insert(E->getType().getAsString()).second){
                newNumaExprs.push_back(E);
            }
    }

    if(const auto *V = result.Nodes.getNodeAs<VarDecl>("varDecl")){
        if(seenVarDeclTypes.insert(V->getType().getAsString()).second){
            numaVarDecls.push_back(V);
        }
    }
}

/**
 * @brief Debug utility to print all found NUMA candidates
 * @param context AST context for pretty printing
 */
void RecursiveNumaTyper::printNumaCandidates(const clang::ASTContext& context)
{
    llvm::outs() << "================= Printing all user defined NUMA heap allocations =================\n";

    for(auto numaExpr : newNumaExprs){
        numaExpr->printPretty(llvm::outs(), nullptr, context.getPrintingPolicy());
        llvm::outs() << "\n";
        llvm::outs() << "Type of NUMA expression is "<< numaExpr->getType().getAsString() << "\n";
    }
    llvm::outs() << "Found " << newNumaExprs.size() << " new numa expressions\n";

    llvm::outs() << "================= Printing all user defined NUMA variable declarations =================\n";
    for(auto varDecl : numaVarDecls){
        varDecl->printName(llvm::outs());
        llvm::outs() << "\n";
        llvm::outs() << "Type of NUMA variable declaration is "<< varDecl->getType().getAsString() << "\n";
    }
}

/**
 * @brief Outputs the transformed source code to the specified stream
 * @param OS Output stream for the rewritten code
 */
void RecursiveNumaTyper::print(llvm::raw_ostream &OS) {
    rewriter.getEditBuffer(rewriter.getSourceMgr().getMainFileID()).write(OS);
}
