#include "checkPersistentTypesConsumer.h"
#include "../transformer/checkPersistentTypes.h"

CheckPersistentTypesConsumer::CheckPersistentTypesConsumer(CheckPersistentTypesTable &table) : table(table) {}

void CheckPersistentTypesConsumer::HandleTranslationUnit(clang::ASTContext &context) {
    CheckPersistentTypesCollector collector(context, table);
    collector.start();
}
