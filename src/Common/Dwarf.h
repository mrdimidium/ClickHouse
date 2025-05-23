#pragma once

#if defined(__ELF__) && !defined(OS_FREEBSD)

/*
 * Copyright 2012-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** This file was edited for ClickHouse.
 *  Original is from folly library.
  */

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>


namespace DB
{

class Elf;

/**
 * DWARF record parser.
 *
 * We only implement enough DWARF functionality to convert from PC address
 * to file and line number information.
 *
 * This means (although they're not part of the public API of this class), we
 * can parse Debug Information Entries (DIEs), abbreviations, attributes (of
 * all forms), and we can interpret bytecode for the line number VM.
 *
 * We can interpret DWARF records of version 2, 3, 4, or 5, although we don't
 * actually support many of the features of versions 4 and 5 (such as VLIW, multiple
 * operations per instruction)
 *
 * Note that the DWARF record parser does not allocate heap memory at all.
 * This is on purpose: you can use the parser from
 * memory-constrained situations (such as an exception handler for
 * std::out_of_memory)  If it weren't for this requirement, some things would
 * be much simpler: the Path class would be unnecessary and would be replaced
 * with a std::string; the list of file names in the line number VM would be
 * kept as a vector of strings instead of re-executing the program to look for
 * DW_LNE_define_file instructions, etc.
 */
class Dwarf final
{
    // Note that Dwarf uses (and returns) std::string_view a lot.
    // The std::string_view point within sections in the ELF file, and so will
    // be live for as long as the passed-in Elf is live.
public:
    /** Create a DWARF parser around an ELF file. */
    explicit Dwarf(const std::shared_ptr<Elf> & elf);

    /**
     * More than one location info may exist if current frame is an inline
     * function call.
     */
    static constexpr uint32_t kMaxInlineLocationInfoPerFrame = 10;

    /**
      * Represent a file path a s collection of three parts (base directory,
      * subdirectory, and file).
      */
    class Path
    {
    public:
        Path() = default;

        Path(std::string_view baseDir, std::string_view subDir, std::string_view file);

        std::string_view baseDir() const { return baseDir_; }
        std::string_view subDir() const { return subDir_; }
        std::string_view file() const { return file_; }

        size_t size() const;

        /**
        * Copy the Path to a buffer of size bufSize.
        *
        * toBuffer behaves like snprintf: It will always null-terminate the
        * buffer (so it will copy at most bufSize-1 bytes), and it will return
        * the number of bytes that would have been written if there had been
        * enough room, so, if toBuffer returns a value >= bufSize, the output
        * was truncated.
        */
        size_t toBuffer(char * buf, size_t bufSize) const;

        void toString(std::string & dest) const;
        std::string toString() const
        {
            std::string s;
            toString(s);
            return s;
        }

        // TODO(tudorb): Implement operator==, operator!=; not as easy as it
        // seems as the same path can be represented in multiple ways
    private:
        std::string_view baseDir_; /// NOLINT
        std::string_view subDir_;  /// NOLINT
        std::string_view file_;    /// NOLINT
    };

    // Indicates inline function `name` is called  at `line@file`.
    struct CallLocation
    {
        Path file = {};
        uint64_t line = 0;
        std::string_view name;
    };

    enum class LocationInfoMode : uint8_t
    {
        // Don't resolve location info.
        DISABLED,
        // Perform CU lookup using .debug_aranges (might be incomplete).
        FAST,
        // Scan all CU in .debug_info (slow!) on .debug_aranges lookup failure.
        FULL,
        // Scan .debug_info (super slower, use with caution) for inline functions in
        // addition to FULL.
        FULL_WITH_INLINE,
    };

    struct LocationInfo
    {
        bool has_main_file = false;
        Path main_file;

        bool has_file_and_line = false;
        Path file;
        uint64_t line = 0;
    };

    /**
     * Frame information: symbol name and location.
     */
    struct SymbolizedFrame
    {
        bool found = false;
        uintptr_t addr = 0;
        // Mangled symbol name. Use `folly::demangle()` to demangle it.
        const char * name = nullptr;
        LocationInfo location;
        std::shared_ptr<const Elf> file;

        void clear() { *this = SymbolizedFrame(); }
    };

    /** Find the file and line number information corresponding to address.
      * The address must be physical - offset in object file without offset in virtual memory where the object is loaded.
      */
    bool findAddress(uintptr_t address, LocationInfo & info, LocationInfoMode mode, std::vector<SymbolizedFrame> & inline_frames) const;

private:
    static bool findDebugInfoOffset(uintptr_t address, std::string_view aranges, uint64_t & offset);

    std::shared_ptr<const Elf> elf_; /// NOLINT

    // DWARF section made up of chunks, each prefixed with a length header.
    // The length indicates whether the chunk is DWARF-32 or DWARF-64, which
    // guides interpretation of "section offset" records.
    // (yes, DWARF-32 and DWARF-64 sections may coexist in the same file)
    class Section
    {
    public:
        Section() : is64_bit(false) {}

        explicit Section(std::string_view d);

        // Return next chunk, if any; the 4- or 12-byte length was already
        // parsed and isn't part of the chunk.
        bool next(std::string_view & chunk);

        // Is the current chunk 64 bit?
        bool is64Bit() const { return is64_bit; }

    private:
        // Yes, 32- and 64- bit sections may coexist.  Yikes!
        bool is64_bit;
        std::string_view data;
    };

    // Abbreviation for a Debugging Information Entry.
    struct DIEAbbreviation
    {
        uint64_t code = 0;
        uint64_t tag = 0;
        bool has_children = false;

        std::string_view attributes;
    };

    // Debugging information entry to define a low-level representation of a
    // source program. Each debugging information entry consists of an identifying
    // tag and a series of attributes. An entry, or group of entries together,
    // provide a description of a corresponding entity in the source program.
    struct Die
    {
        bool is64Bit; /// NOLINT
        // Offset from start to first attribute
        uint8_t attr_offset;
        // Offset within debug info.
        uint64_t offset;
        uint64_t code;
        DIEAbbreviation abbr;
    };

    struct AttributeSpec
    {
        uint64_t name = 0;
        uint64_t form = 0;
        int64_t implicitConst = 0; // only set when form=DW_FORM_implicit_const

        explicit operator bool() const { return name != 0 || form != 0; }
    };

    struct Attribute
    {
        AttributeSpec spec;
        const Die & die;
        std::variant<uint64_t, std::string_view> attr_value;
    };

    enum
    {
        DW_UT_compile = 0x01,
        DW_UT_skeleton = 0x04,
    };

    struct CompilationUnit
    {
        bool is64Bit = false; /// NOLINT
        uint8_t version = 0;
        uint8_t unit_type = DW_UT_compile; // DW_UT_compile or DW_UT_skeleton
        uint8_t addr_size = 0;
        // Offset in .debug_info of this compilation unit.
        uint64_t offset = 0;
        uint64_t size = 0;
        // Offset in .debug_info for the first DIE in this compilation unit.
        uint64_t first_die = 0;
        uint64_t abbrev_offset = 0;

        // The beginning of the CU's contribution to .debug_addr
        std::optional<uint64_t> addr_base; // DW_AT_addr_base (DWARF 5)
        // The beginning of the offsets table (immediately following the
        // header) of the CU's contribution to .debug_loclists
        std::optional<uint64_t> loclists_base; // DW_AT_loclists_base (DWARF 5)
        // The beginning of the offsets table (immediately following the
        // header) of the CU's contribution to .debug_rnglists
        std::optional<uint64_t> rnglists_base; // DW_AT_rnglists_base (DWARF 5)
        // Points to the first string offset of the compilation unit's
        // contribution to the .debug_str_offsets (or .debug_str_offsets.dwo) section.
        std::optional<uint64_t> str_offsets_base; // DW_AT_str_offsets_base (DWARF 5)

        // Only the CompilationUnit that contains the caller functions needs this cache.
        // Indexed by (abbr.code - 1) if (abbr.code - 1) < abbrCache.size();
        std::vector<DIEAbbreviation> abbr_cache;
    };

    /** cu must exist during the life cycle of created Die. */
    Die getDieAtOffset(const CompilationUnit & cu, uint64_t offset) const;

    bool findLocation(
        uintptr_t address,
        LocationInfoMode mode,
        CompilationUnit & cu,
        LocationInfo & info,
        std::vector<SymbolizedFrame> & inline_frames,
        bool assume_in_cu_range) const;

    /**
     * Finds a subprogram debugging info entry that contains a given address among
     * children of given die. Depth first search.
     */
    void findSubProgramDieForAddress(
        const CompilationUnit & cu, const Die & die, uint64_t address, std::optional<uint64_t> base_addr_cu, Die & subprogram) const;

    // Interpreter for the line number bytecode VM
    class LineNumberVM
    {
    public:
        LineNumberVM(
            std::string_view data,
            std::string_view compilationDirectory,
            std::string_view debugStr,
            std::string_view debugLineStr);

        bool findAddress(uintptr_t target, Path & file, uint64_t & line);

        /** Gets full file name at given index including directory. */
        Path getFullFileName(uint64_t index) const;

    private:
        void init();
        void reset();

        // Execute until we commit one new row to the line number matrix
        bool next(std::string_view & program);
        enum StepResult
        {
            CONTINUE, // Continue feeding opcodes
            COMMIT, // Commit new <address, file, line> tuple
            END, // End of sequence
        };
        // Execute one opcode
        StepResult step(std::string_view & program);

        struct FileName
        {
            std::string_view relativeName; /// NOLINT
            // 0 = current compilation directory
            // otherwise, 1-based index in the list of include directories
            uint64_t directoryIndex; /// NOLINT
        };
        // Read one FileName object, remove_prefix program
        static bool readFileName(std::string_view & program, FileName & fn);

        // Get file name at given index; may be in the initial table
        // (fileNames_) or defined using DW_LNE_define_file (and we reexecute
        // enough of the program to find it, if so)
        FileName getFileName(uint64_t index) const;

        // Get include directory at given index
        std::string_view getIncludeDirectory(uint64_t index) const;

        // Execute opcodes until finding a DW_LNE_define_file and return true;
        // return file at the end.
        bool nextDefineFile(std::string_view & program, FileName & fn) const;

        // Initialization
        bool is64Bit_;                                    /// NOLINT
        std::string_view data_;                           /// NOLINT
        std::string_view compilationDirectory_;           /// NOLINT
        std::string_view debugStr_; // needed for DWARF 5 /// NOLINT
        std::string_view debugLineStr_; // DWARF 5        /// NOLINT

        // Header
        uint16_t version_;   /// NOLINT
        uint8_t minLength_;  /// NOLINT
        bool defaultIsStmt_; /// NOLINT
        int8_t lineBase_;    /// NOLINT
        uint8_t lineRange_;  /// NOLINT
        uint8_t opcodeBase_; /// NOLINT
        const uint8_t * standardOpcodeLengths_; /// NOLINT

        // 6.2.4 The Line Number Program Header.
        struct
        {
            size_t includeDirectoryCount;
            std::string_view includeDirectories;
            size_t fileNameCount;
            std::string_view fileNames;
        } v4_;

        struct
        {
            uint8_t directoryEntryFormatCount;
            std::string_view directoryEntryFormat;
            uint64_t directoriesCount;
            std::string_view directories;

            uint8_t fileNameEntryFormatCount;
            std::string_view fileNameEntryFormat;
            uint64_t fileNamesCount;
            std::string_view fileNames;
        } v5_;

        // State machine registers
        uint64_t address_; /// NOLINT
        uint64_t file_; /// NOLINT
        uint64_t line_; /// NOLINT
        uint64_t column_; /// NOLINT
        bool isStmt_; /// NOLINT
        bool basicBlock_; /// NOLINT
        bool endSequence_; /// NOLINT
        bool prologueEnd_; /// NOLINT
        bool epilogueBegin_; /// NOLINT
        uint64_t isa_; /// NOLINT
        uint64_t discriminator_; /// NOLINT
    };

    /**
     * Finds inlined subroutine DIEs and their caller lines that contains a given
     * address among children of given die. Depth first search.
     */
    void findInlinedSubroutineDieForAddress(
        const CompilationUnit & cu,
        const Die & die,
        const LineNumberVM & line_vm,
        uint64_t address,
        std::optional<uint64_t> base_addr_cu,
        std::vector<CallLocation> & locations,
        size_t max_size) const;

    // Read an abbreviation from a std::string_view, return true if at end; remove_prefix section
    static bool readAbbreviation(std::string_view & section, DIEAbbreviation & abbr);

    static void readCompilationUnitAbbrs(std::string_view abbrev, CompilationUnit & cu);

    /**
     * Iterates over all children of a debugging info entry, calling the given
     * callable for each. Iteration is stopped early if any of the calls return
     * false. Returns the offset of next DIE after iterations.
     */
    size_t forEachChild(const CompilationUnit & cu, const Die & die, std::function<bool(const Die & die)> f) const;

    // Get abbreviation corresponding to a code, in the chunk starting at
    // offset in the .debug_abbrev section
    DIEAbbreviation getAbbreviation(uint64_t code, uint64_t offset) const;

    /**
     * Iterates over all attributes of a debugging info entry, calling the given
     * callable for each. If all attributes are visited, then return the offset of
     * next DIE, or else iteration is stopped early and return size_t(-1) if any
     * of the calls return false.
     */
    size_t forEachAttribute(const CompilationUnit & cu, const Die & die, std::function<bool(const Attribute & die)> f) const;

    Attribute readAttribute(
        const CompilationUnit & cu,
        const Die & die,
        AttributeSpec spec,
        std::string_view & info) const;

    // Read one attribute <name, form> pair, remove_prefix sp; returns <0, 0> at end.
    static AttributeSpec readAttributeSpec(std::string_view & sp);

    // Read one attribute value, remove_prefix sp
    using AttributeValue = std::variant<uint64_t, std::string_view>;
    AttributeValue readAttributeValue(std::string_view & sp, uint64_t form, bool is64_bit) const;

    // Get an ELF section by name, return true if found
    std::string_view getSection(const char * name) const;

    CompilationUnit getCompilationUnit(uint64_t offset) const;
    // Finds the Compilation Unit starting at offset.
    CompilationUnit findCompilationUnit(uint64_t targetOffset) const;

    // Parses an attribute of "reference" form class, i.e. a reference to another DIE.
    // Returns the unit containing the target DIE (nullopt if it's in the same unit as the source DIE)
    // and the offset of the target DIE (relative to .debug_info, not to unit).
    std::optional<std::pair<std::optional<CompilationUnit>, uint64_t>> getReferenceAttribute(
        const CompilationUnit & cu, const Die & die, uint64_t attr_name) const;

    // Check if the given address is in the range list at the given offset in .debug_ranges.
    bool isAddrInRangeList(
        const CompilationUnit & cu,
        uint64_t address,
        std::optional<uint64_t> base_addr,
        size_t offset,
        uint8_t addr_size) const;

    std::string_view abbrev_; // .debug_abbrev                     /// NOLINT
    std::string_view addr_; // .debug_addr (DWARF 5)               /// NOLINT
    std::string_view aranges_; // .debug_aranges                   /// NOLINT
    std::string_view info_; // .debug_info                         /// NOLINT
    std::string_view line_; // .debug_line                         /// NOLINT
    std::string_view line_str_; // .debug_line_str (DWARF 5)       /// NOLINT
    std::string_view loclists_; // .debug_loclists (DWARF 5)       /// NOLINT
    std::string_view ranges_; // .debug_ranges                     /// NOLINT
    std::string_view rnglists_; // .debug_rnglists (DWARF 5)       /// NOLINT
    std::string_view str_; // .debug_str                           /// NOLINT
    std::string_view str_offsets_; // .debug_str_offsets (DWARF 5) /// NOLINT
};

}

#endif
