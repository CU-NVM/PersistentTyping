#include "rootSetupConsumer.h"
#include "../transformer/rootSetup.h"

RootSetupConsumer::RootSetupConsumer(RootTable &table) : table(table) {}

void RootSetupConsumer::HandleTranslationUnit(clang::ASTContext &context) {
    RootCollector collector(context, table);
    collector.start();
}
