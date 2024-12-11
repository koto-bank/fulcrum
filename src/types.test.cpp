#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "codegen_context.hpp"
#include "types.hpp"

struct TypeTest : public testing::Test {
    TypeTest() {
        ctx = std::make_unique<CodegenContext>("", llvmCtx);
        i8 = emplaceType<IntegerType>(8, true);
        i16 = emplaceType<IntegerType>(16, true);
        i32 = emplaceType<IntegerType>(32, true);
        i64 = emplaceType<IntegerType>(64, true);

        u8 = emplaceType<IntegerType>(8, false);
        u16 = emplaceType<IntegerType>(16, false);
        u32 = emplaceType<IntegerType>(32, false);
        u64 = emplaceType<IntegerType>(64, false);

        voidType = emplaceType<VoidType>();
        boolType = emplaceType<BoolType>();

        arrayi8 = emplaceType<ArrayType>(i8, 8);
        arrayi16 = emplaceType<ArrayType>(i16, 8);
        ptri8 = emplaceType<PointerType>(i8);
        ptri16 = emplaceType<PointerType>(i16);

        str = emplaceType<AliasType>("str", ptri8);
        strAlias = emplaceType<AliasType>("str-other", str);
        str16 = emplaceType<AliasType>("str16", ptri16);

        strArray = emplaceType<ArrayType>(str, 8);
        ptri8Array = emplaceType<ArrayType>(ptri8, 8);
        strPtr = emplaceType<PointerType>(str);
    }

    template <typename T, typename ...Args>
    const T * emplaceType(Args &&...args) {
        auto val = std::make_unique<T>(*ctx, std::forward<Args>(args)...);
        return static_cast<const T *>(allTypes.emplace_back(std::move(val)).get());
    }

    llvm::LLVMContext llvmCtx;
    std::unique_ptr<CodegenContext> ctx;

    const IntegerType *i8;
    const IntegerType *i16;
    const IntegerType *i32;
    const IntegerType *i64;
    const IntegerType *u8;
    const IntegerType *u16;
    const IntegerType *u32;
    const IntegerType *u64;

    const VoidType *voidType;
    const BoolType *boolType;

    const ArrayType *arrayi8;
    const ArrayType *arrayi16;
    const PointerType *ptri8;
    const PointerType *ptri16;

    const AliasType *str;
    const AliasType *strAlias;
    const AliasType *str16;

    const ArrayType *strArray;
    const ArrayType *ptri8Array;
    const PointerType *strPtr;

    const ArrayType *strArrayAlias;
    const PointerType *strPtrAlias;

    std::vector<std::unique_ptr<LanguageType>> allTypes;
};

TEST_F(TypeTest, void) {
    EXPECT_FALSE(LanguageType::assignable(i8, voidType));
    EXPECT_FALSE(LanguageType::assignable(voidType, i8));
    EXPECT_FALSE(LanguageType::assignable(voidType, voidType));
}

TEST_F(TypeTest, integerTypes) {
    EXPECT_TRUE(LanguageType::assignable(u8, u8));
    EXPECT_TRUE(LanguageType::assignable(u16, u8));
    EXPECT_TRUE(LanguageType::assignable(u32, u8));
    EXPECT_TRUE(LanguageType::assignable(u64, u8));

    EXPECT_TRUE(LanguageType::assignable(i8, u8));
    EXPECT_TRUE(LanguageType::assignable(i16, u8));
    EXPECT_TRUE(LanguageType::assignable(i32, u8));
    EXPECT_TRUE(LanguageType::assignable(i64, u8));

    EXPECT_TRUE(LanguageType::assignable(u16, u8));
    EXPECT_TRUE(LanguageType::assignable(u16, u16));
    EXPECT_FALSE(LanguageType::assignable(u16, u32));
    EXPECT_FALSE(LanguageType::assignable(u16, u64));

    EXPECT_TRUE(LanguageType::assignable(u16, i8));
    EXPECT_TRUE(LanguageType::assignable(u16, i16));
    EXPECT_FALSE(LanguageType::assignable(u16, i32));
    EXPECT_FALSE(LanguageType::assignable(u16, i64));
}

TEST_F(TypeTest, pointersAndArrays) {
    EXPECT_TRUE(LanguageType::assignable(ptri8, ptri8));
    EXPECT_FALSE(LanguageType::assignable(ptri16, ptri8));
    EXPECT_FALSE(LanguageType::assignable(ptri8, ptri16));

    EXPECT_TRUE(LanguageType::assignable(ptri8, arrayi8));
    EXPECT_FALSE(LanguageType::assignable(arrayi8, ptri8));
    EXPECT_TRUE(LanguageType::assignable(arrayi8, arrayi8));
    EXPECT_FALSE(LanguageType::assignable(arrayi8, arrayi16));
}

TEST_F(TypeTest, aliasValues) {
    EXPECT_TRUE(LanguageType::assignable(strAlias, str));
    EXPECT_TRUE(LanguageType::assignable(str, strAlias));
    EXPECT_TRUE(LanguageType::assignable(ptri8, str));
    EXPECT_TRUE(LanguageType::assignable(str, ptri8));
    EXPECT_FALSE(LanguageType::assignable(str16, str));
    EXPECT_FALSE(LanguageType::assignable(str, str16));
    EXPECT_TRUE(LanguageType::assignable(str16, ptri16));

    EXPECT_TRUE(LanguageType::assignable(strPtr, strArray));
    EXPECT_FALSE(LanguageType::assignable(strArray, strPtr));
    EXPECT_TRUE(LanguageType::assignable(strPtr, ptri8Array));
    EXPECT_TRUE(LanguageType::assignable(ptri8Array, strArray));
}
