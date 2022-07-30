#pragma once

enum Tag
{
	DW_TAG_array_type = 0x01,
	DW_TAG_class_type = 0x02,
	DW_TAG_entry_point = 0x03,
	DW_TAG_enumeration_type = 0x04,
	DW_TAG_formal_parameter = 0x05,
	DW_TAG_imported_declaration = 0x08,
	DW_TAG_label = 0x0a,
	DW_TAG_lexical_block = 0x0b,
	DW_TAG_member = 0x0d,
	DW_TAG_pointer_type = 0x0f,
	DW_TAG_reference_type = 0x10,
	DW_TAG_compile_unit = 0x11,
	DW_TAG_string_type = 0x12,
	DW_TAG_structure_type = 0x13,
	DW_TAG_subroutine_type = 0x15,
	DW_TAG_typedef = 0x16,
	DW_TAG_union_type = 0x17,
	DW_TAG_unspecified_parameters = 0x18,
	DW_TAG_variant = 0x19,
	DW_TAG_common_block = 0x1a,
	DW_TAG_common_inclusion = 0x1b,
	DW_TAG_inheritance = 0x1c,
	DW_TAG_inlined_subroutine = 0x1d,
	DW_TAG_module = 0x1e,
	DW_TAG_ptr_to_member_type = 0x1f,
	DW_TAG_set_type = 0x20,
	DW_TAG_subrange_type = 0x21,
	DW_TAG_with_stmt = 0x22,
	DW_TAG_access_declaration = 0x23,
	DW_TAG_base_type = 0x24,
	DW_TAG_catch_block = 0x25,
	DW_TAG_const_type = 0x26,
	DW_TAG_constant = 0x27,
	DW_TAG_enumerator = 0x28,
	DW_TAG_file_type = 0x29,
	DW_TAG_friend = 0x2a,
	DW_TAG_namelist = 0x2b,
	DW_TAG_namelist_item = 0x2c,
	DW_TAG_packed_type = 0x2d,
	DW_TAG_subprogram = 0x2e,
	DW_TAG_template_type_parameter = 0x2f,
	DW_TAG_template_value_parameter = 0x30,
	DW_TAG_thrown_type = 0x31,
	DW_TAG_try_block = 0x32,
	DW_TAG_variant_part = 0x33,
	DW_TAG_variable = 0x34,
	DW_TAG_volatile_type = 0x35,
	DW_TAG_dwarf_procedure = 0x36,
	DW_TAG_restrict_type = 0x37,
	DW_TAG_interface_type = 0x38,
	DW_TAG_namespace = 0x39,
	DW_TAG_imported_module = 0x3a,
	DW_TAG_unspecified_type = 0x3b,
	DW_TAG_partial_unit = 0x3c,
	DW_TAG_imported_unit = 0x3d,
	DW_TAG_condition = 0x3f,
	DW_TAG_shared_type = 0x40,
	DW_TAG_type_unit = 0x41,
	DW_TAG_rvalue_reference_type = 0x42,
	DW_TAG_template_alias = 0x43,

	// New in DbgModule 5:
	DW_TAG_coarray_type = 0x44,
	DW_TAG_generic_subrange = 0x45,
	DW_TAG_dynamic_type = 0x46,

	DW_TAG_MIPS_loop = 0x4081,
	DW_TAG_format_label = 0x4101,
	DW_TAG_function_template = 0x4102,
	DW_TAG_class_template = 0x4103,
	DW_TAG_GNU_template_template_param = 0x4106,
	DW_TAG_GNU_template_parameter_pack = 0x4107,
	DW_TAG_GNU_formal_parameter_pack = 0x4108,
	DW_TAG_lo_user = 0x4080,
	DW_TAG_APPLE_property = 0x4200,
	DW_TAG_hi_user = 0xffff
};

enum Attribute
{
	// Attributes
	DW_AT_sibling = 0x01,
	DW_AT_location = 0x02,
	DW_AT_name = 0x03,
	DW_AT_ordering = 0x09,
	DW_AT_byte_size = 0x0b,
	DW_AT_bit_offset = 0x0c,
	DW_AT_bit_size = 0x0d,
	DW_AT_stmt_list = 0x10,
	DW_AT_low_pc = 0x11,
	DW_AT_high_pc = 0x12,
	DW_AT_language = 0x13,
	DW_AT_discr = 0x15,
	DW_AT_discr_value = 0x16,
	DW_AT_visibility = 0x17,
	DW_AT_import = 0x18,
	DW_AT_string_length = 0x19,
	DW_AT_common_reference = 0x1a,
	DW_AT_comp_dir = 0x1b,
	DW_AT_const_value = 0x1c,
	DW_AT_containing_type = 0x1d,
	DW_AT_default_value = 0x1e,
	DW_AT_inline = 0x20,
	DW_AT_is_optional = 0x21,
	DW_AT_lower_bound = 0x22,
	DW_AT_producer = 0x25,
	DW_AT_prototyped = 0x27,
	DW_AT_return_addr = 0x2a,
	DW_AT_start_scope = 0x2c,
	DW_AT_bit_stride = 0x2e,
	DW_AT_upper_bound = 0x2f,
	DW_AT_abstract_origin = 0x31,
	DW_AT_accessibility = 0x32,
	DW_AT_address_class = 0x33,
	DW_AT_artificial = 0x34,
	DW_AT_base_types = 0x35,
	DW_AT_calling_convention = 0x36,
	DW_AT_count = 0x37,
	DW_AT_data_member_location = 0x38,
	DW_AT_decl_column = 0x39,
	DW_AT_decl_file = 0x3a,
	DW_AT_decl_line = 0x3b,
	DW_AT_declaration = 0x3c,
	DW_AT_discr_list = 0x3d,
	DW_AT_encoding = 0x3e,
	DW_AT_external = 0x3f,
	DW_AT_frame_base = 0x40,
	DW_AT_friend = 0x41,
	DW_AT_identifier_case = 0x42,
	DW_AT_macro_info = 0x43,
	DW_AT_namelist_item = 0x44,
	DW_AT_priority = 0x45,
	DW_AT_segment = 0x46,
	DW_AT_specification = 0x47,
	DW_AT_static_link = 0x48,
	DW_AT_type = 0x49,
	DW_AT_use_location = 0x4a,
	DW_AT_variable_parameter = 0x4b,
	DW_AT_virtuality = 0x4c,
	DW_AT_vtable_elem_location = 0x4d,
	DW_AT_allocated = 0x4e,
	DW_AT_associated = 0x4f,
	DW_AT_data_location = 0x50,
	DW_AT_byte_stride = 0x51,
	DW_AT_entry_pc = 0x52,
	DW_AT_use_UTF8 = 0x53,
	DW_AT_extension = 0x54,
	DW_AT_ranges = 0x55,
	DW_AT_trampoline = 0x56,
	DW_AT_call_column = 0x57,
	DW_AT_call_file = 0x58,
	DW_AT_call_line = 0x59,
	DW_AT_description = 0x5a,
	DW_AT_binary_scale = 0x5b,
	DW_AT_decimal_scale = 0x5c,
	DW_AT_small = 0x5d,
	DW_AT_decimal_sign = 0x5e,
	DW_AT_digit_count = 0x5f,
	DW_AT_picture_string = 0x60,
	DW_AT_mutable = 0x61,
	DW_AT_threads_scaled = 0x62,
	DW_AT_explicit = 0x63,
	DW_AT_object_pointer = 0x64,
	DW_AT_endianity = 0x65,
	DW_AT_elemental = 0x66,
	DW_AT_pure = 0x67,
	DW_AT_recursive = 0x68,
	DW_AT_signature = 0x69,
	DW_AT_main_subprogram = 0x6a,
	DW_AT_data_bit_offset = 0x6b,
	DW_AT_const_expr = 0x6c,
	DW_AT_enum_class = 0x6d,
	DW_AT_linkage_name = 0x6e,

	// New in DbgModule 5:
	DW_AT_string_length_bit_size = 0x6f,
	DW_AT_string_length_byte_size = 0x70,
	DW_AT_rank = 0x71,
	DW_AT_str_offsets_base = 0x72,
	DW_AT_addr_base = 0x73,
	DW_AT_ranges_base = 0x74,
	DW_AT_dwo_id = 0x75,
	DW_AT_dwo_name = 0x76,
	DW_AT_reference = 0x77,
	DW_AT_rvalue_reference = 0x78,

	DW_AT_lo_user = 0x2000,
	DW_AT_hi_user = 0x3fff,

	DW_AT_MIPS_loop_begin = 0x2002,
	DW_AT_MIPS_tail_loop_begin = 0x2003,
	DW_AT_MIPS_epilog_begin = 0x2004,
	DW_AT_MIPS_loop_unroll_factor = 0x2005,
	DW_AT_MIPS_software_pipeline_depth = 0x2006,
	DW_AT_MIPS_linkage_name = 0x2007,
	DW_AT_MIPS_stride = 0x2008,
	DW_AT_MIPS_abstract_name = 0x2009,
	DW_AT_MIPS_clone_origin = 0x200a,
	DW_AT_MIPS_has_inlines = 0x200b,
	DW_AT_MIPS_stride_byte = 0x200c,
	DW_AT_MIPS_stride_elem = 0x200d,
	DW_AT_MIPS_ptr_dopetype = 0x200e,
	DW_AT_MIPS_allocatable_dopetype = 0x200f,
	DW_AT_MIPS_assumed_shape_dopetype = 0x2010,

	// This one appears to have only been implemented by Open64 for
	// fortran and may conflict with other extensions.
	DW_AT_MIPS_assumed_size = 0x2011,

	// GNU extensions
	DW_AT_sf_names = 0x2101,
	DW_AT_src_info = 0x2102,
	DW_AT_mac_info = 0x2103,
	DW_AT_src_coords = 0x2104,
	DW_AT_body_begin = 0x2105,
	DW_AT_body_end = 0x2106,
	DW_AT_GNU_vector = 0x2107,
	DW_AT_GNU_template_name = 0x2110,

	DW_AT_GNU_odr_signature = 0x210f,

	// Extensions for Fission proposal.
	DW_AT_GNU_dwo_name = 0x2130,
	DW_AT_GNU_dwo_id = 0x2131,
	DW_AT_GNU_ranges_base = 0x2132,
	DW_AT_GNU_addr_base = 0x2133,
	DW_AT_GNU_pubnames = 0x2134,
	DW_AT_GNU_pubtypes = 0x2135,

	// Apple extensions.
	DW_AT_APPLE_optimized = 0x3fe1,
	DW_AT_APPLE_flags = 0x3fe2,
	DW_AT_APPLE_isa = 0x3fe3,
	DW_AT_APPLE_block = 0x3fe4,
	DW_AT_APPLE_major_runtime_vers = 0x3fe5,
	DW_AT_APPLE_runtime_class = 0x3fe6,
	DW_AT_APPLE_omit_frame_ptr = 0x3fe7,
	DW_AT_APPLE_property_name = 0x3fe8,
	DW_AT_APPLE_property_getter = 0x3fe9,
	DW_AT_APPLE_property_setter = 0x3fea,
	DW_AT_APPLE_property_attribute = 0x3feb,
	DW_AT_APPLE_objc_complete_type = 0x3fec,
	DW_AT_APPLE_property = 0x3fed
};

enum Constants
{
	// Children flag
	DW_CHILDREN_no = 0x00,
	DW_CHILDREN_yes = 0x01,

	DW_EH_PE_absptr = 0x00,
	DW_EH_PE_omit = 0xff,
	DW_EH_PE_uleb128 = 0x01,
	DW_EH_PE_udata2 = 0x02,
	DW_EH_PE_udata4 = 0x03,
	DW_EH_PE_udata8 = 0x04,
	DW_EH_PE_sleb128 = 0x09,
	DW_EH_PE_sdata2 = 0x0A,
	DW_EH_PE_sdata4 = 0x0B,
	DW_EH_PE_sdata8 = 0x0C,
	DW_EH_PE_signed = 0x08,
	DW_EH_PE_pcrel = 0x10,
	DW_EH_PE_textrel = 0x20,
	DW_EH_PE_datarel = 0x30,
	DW_EH_PE_funcrel = 0x40,
	DW_EH_PE_aligned = 0x50,
	DW_EH_PE_indirect = 0x80
};

enum Forms
{
	// Attribute form encodings
	DW_FORM_addr = 0x01,
	DW_FORM_block2 = 0x03,
	DW_FORM_block4 = 0x04,
	DW_FORM_data2 = 0x05,
	DW_FORM_data4 = 0x06,
	DW_FORM_data8 = 0x07,
	DW_FORM_string = 0x08,
	DW_FORM_block = 0x09,
	DW_FORM_block1 = 0x0a,
	DW_FORM_data1 = 0x0b,
	DW_FORM_flag = 0x0c,
	DW_FORM_sdata = 0x0d,
	DW_FORM_strp = 0x0e,
	DW_FORM_udata = 0x0f,
	DW_FORM_ref_addr = 0x10,
	DW_FORM_ref1 = 0x11,
	DW_FORM_ref2 = 0x12,
	DW_FORM_ref4 = 0x13,
	DW_FORM_ref8 = 0x14,
	DW_FORM_ref_udata = 0x15,
	DW_FORM_indirect = 0x16,
	DW_FORM_sec_offset = 0x17,
	DW_FORM_exprloc = 0x18,
	DW_FORM_flag_present = 0x19,
	DW_FORM_ref_sig8 = 0x20,

	// Extensions for Fission proposal
	DW_FORM_GNU_addr_index = 0x1f01,
	DW_FORM_GNU_str_index = 0x1f02
};

enum LineNumberOps
{
	// Line Number Standard Opcode Encodings
	DW_LNS_extended_op = 0x00,
	DW_LNS_copy = 0x01,
	DW_LNS_advance_pc = 0x02,
	DW_LNS_advance_line = 0x03,
	DW_LNS_set_file = 0x04,
	DW_LNS_set_column = 0x05,
	DW_LNS_negate_stmt = 0x06,
	DW_LNS_set_basic_block = 0x07,
	DW_LNS_const_add_pc = 0x08,
	DW_LNS_fixed_advance_pc = 0x09,
	DW_LNS_set_prologue_end = 0x0a,
	DW_LNS_set_epilogue_begin = 0x0b,
	DW_LNS_set_isa = 0x0c
};

enum LineNumberExtendedOps
{
	// Line Number Extended Opcode Encodings
	DW_LNE_end_sequence = 0x01,
	DW_LNE_set_address = 0x02,
	DW_LNE_define_file = 0x03,
	DW_LNE_set_discriminator = 0x04,
	DW_LNE_lo_user = 0x80,
	DW_LNE_hi_user = 0xff
};

enum
{
	// Encoding attribute values
	DW_ATE_address = 0x01,
	DW_ATE_boolean = 0x02,
	DW_ATE_complex_float = 0x03,
	DW_ATE_float = 0x04,
	DW_ATE_signed = 0x05,
	DW_ATE_signed_char = 0x06,
	DW_ATE_unsigned = 0x07,
	DW_ATE_unsigned_char = 0x08,
	DW_ATE_imaginary_float = 0x09,
	DW_ATE_packed_decimal = 0x0a,
	DW_ATE_numeric_string = 0x0b,
	DW_ATE_edited = 0x0c,
	DW_ATE_signed_fixed = 0x0d,
	DW_ATE_unsigned_fixed = 0x0e,
	DW_ATE_decimal_float = 0x0f,
	DW_ATE_UTF = 0x10,
	DW_ATE_lo_user = 0x80,
	DW_ATE_hi_user = 0xff,
};

// Operation encodings
enum
{
	DW_OP_addr = 0x03,
	DW_OP_deref = 0x06,
	DW_OP_const1u = 0x08,
	DW_OP_const1s = 0x09,
	DW_OP_const2u = 0x0a,
	DW_OP_const2s = 0x0b,
	DW_OP_const4u = 0x0c,
	DW_OP_const4s = 0x0d,
	DW_OP_const8u = 0x0e,
	DW_OP_const8s = 0x0f,
	DW_OP_constu = 0x10,
	DW_OP_consts = 0x11,
	DW_OP_dup = 0x12,
	DW_OP_drop = 0x13,
	DW_OP_over = 0x14,
	DW_OP_pick = 0x15,
	DW_OP_swap = 0x16,
	DW_OP_rot = 0x17,
	DW_OP_xderef = 0x18,
	DW_OP_abs = 0x19,
	DW_OP_and = 0x1a,
	DW_OP_div = 0x1b,
	DW_OP_minus = 0x1c,
	DW_OP_mod = 0x1d,
	DW_OP_mul = 0x1e,
	DW_OP_neg = 0x1f,
	DW_OP_not = 0x20,
	DW_OP_or = 0x21,
	DW_OP_plus = 0x22,
	DW_OP_plus_uconst = 0x23,
	DW_OP_shl = 0x24,
	DW_OP_shr = 0x25,
	DW_OP_shra = 0x26,
	DW_OP_xor = 0x27,
	DW_OP_skip = 0x2f,
	DW_OP_bra = 0x28,
	DW_OP_eq = 0x29,
	DW_OP_ge = 0x2a,
	DW_OP_gt = 0x2b,
	DW_OP_le = 0x2c,
	DW_OP_lt = 0x2d,
	DW_OP_ne = 0x2e,
	DW_OP_lit0 = 0x30,
	DW_OP_lit1 = 0x31,
	DW_OP_lit31 = 0x4f,
	DW_OP_reg0 = 0x50,
	DW_OP_reg1 = 0x51,
	DW_OP_reg2 = 0x52,
	DW_OP_reg3 = 0x53,
	DW_OP_reg4 = 0x54,
	DW_OP_reg5 = 0x55,
	DW_OP_reg6 = 0x56,
	DW_OP_reg7 = 0x57,
	DW_OP_reg8 = 0x58,
	DW_OP_reg9 = 0x59,
	DW_OP_reg10 = 0x5a,
	DW_OP_reg11 = 0x5b,
	DW_OP_reg12 = 0x5c,
	DW_OP_reg13 = 0x5d,
	DW_OP_reg14 = 0x5e,
	DW_OP_reg15 = 0x5f,
	DW_OP_reg16 = 0x60,
	DW_OP_reg17 = 0x61,
	DW_OP_reg18 = 0x62,
	DW_OP_reg19 = 0x63,
	DW_OP_reg20 = 0x64,
	DW_OP_reg21 = 0x65,
	DW_OP_reg22 = 0x66,
	DW_OP_reg23 = 0x66,
	DW_OP_reg24 = 0x68,
	DW_OP_reg25 = 0x69,
	DW_OP_reg26 = 0x6a,
	DW_OP_reg27 = 0x6b,
	DW_OP_reg28 = 0x6c,
	DW_OP_reg29 = 0x6d,
	DW_OP_reg30 = 0x6e,
	DW_OP_reg31 = 0x6f,
	DW_OP_breg0 = 0x70,
	DW_OP_breg1 = 0x71,
	DW_OP_breg2 = 0x72,
	DW_OP_breg3 = 0x73,
	DW_OP_breg4 = 0x74,
	DW_OP_breg5 = 0x75,
	DW_OP_breg6 = 0x76,
	DW_OP_breg7 = 0x77,
	DW_OP_breg8 = 0x78,
	DW_OP_breg9 = 0x79,
	DW_OP_breg10 = 0x7A,
	DW_OP_breg11 = 0x7B,
	DW_OP_breg12 = 0x7C,
	DW_OP_breg13 = 0x7D,
	DW_OP_breg14 = 0x7E,
	DW_OP_breg15 = 0x7F,
	DW_OP_breg31 = 0x8f,
	DW_OP_regx = 0x90,
	DW_OP_fbreg = 0x91,
	DW_OP_bregx = 0x92,
	DW_OP_piece = 0x93,
	DW_OP_deref_size = 0x94,
	DW_OP_xderef_size = 0x95,
	DW_OP_nop = 0x96,
	DW_OP_push_object_address = 0x97,
	DW_OP_call2 = 0x98,
	DW_OP_call4 = 0x99,
	DW_OP_call_ref = 0x9a,
	DW_OP_form_tls_address = 0x9b,
	DW_OP_call_frame_cfa = 0x9c,
	DW_OP_implicit_value = 0x9e,
	DW_OP_stack_value = 0x9f,
	DW_OP_lo_user = 0xe0,
	DW_OP_GNU_push_tls_address = 0xe0,
	DW_OP_hi_user = 0xff,

	DW_OP_addr_noRemap = 0xf0
};

// Call frame instruction encodings
enum
{
	DW_CFA_extended = 0x00,
	DW_CFA_nop = 0x00,
	DW_CFA_advance_loc = 0x40,
	DW_CFA_offset0 = 0x80,
	DW_CFA_offset1 = 0x81,
	DW_CFA_offset2 = 0x82,
	DW_CFA_offset3 = 0x83,
	DW_CFA_offset4 = 0x84,
	DW_CFA_offset5 = 0x85,
	DW_CFA_offset6 = 0x86,
	DW_CFA_offset7 = 0x87,
	DW_CFA_offset8 = 0x88,
	DW_CFA_offset = 0x80,
	DW_CFA_restore = 0xc0,
	DW_CFA_set_loc = 0x01,
	DW_CFA_advance_loc1 = 0x02,
	DW_CFA_advance_loc2 = 0x03,
	DW_CFA_advance_loc4 = 0x04,
	DW_CFA_offset_extended = 0x05,
	DW_CFA_restore_extended = 0x06,
	DW_CFA_undefined = 0x07,
	DW_CFA_same_value = 0x08,
	DW_CFA_register = 0x09,
	DW_CFA_remember_state = 0x0a,
	DW_CFA_restore_state = 0x0b,
	DW_CFA_def_cfa = 0x0c,
	DW_CFA_def_cfa_register = 0x0d,
	DW_CFA_def_cfa_offset = 0x0e,
	DW_CFA_def_cfa_expression = 0x0f,
	DW_CFA_expression = 0x10,
	DW_CFA_offset_extended_sf = 0x11,
	DW_CFA_def_cfa_sf = 0x12,
	DW_CFA_def_cfa_offset_sf = 0x13,
	DW_CFA_val_offset = 0x14,
	DW_CFA_val_offset_sf = 0x15,
	DW_CFA_val_expression = 0x16,
	DW_CFA_lo_user = 0x1c,
	DW_CFA_hi_user = 0x3f,
};

enum SymbolFlags
{
	SF_TypeMask = 0x0000FFFF,
	SF_TypeShift = 0,

	SF_ClassMask = 0x00FF0000,
	SF_ClassShift = 16,

	SF_WeakExternal = 0x01000000
};

enum SymbolSectionNumber
{
	COFF_SYM_DEBUG = -2,
	COFF_SYM_ABSOLUTE = -1,
	COFF_SYM_UNDEFINED = 0
};

/// Storage class tells where and what the symbol represents
enum SymbolStorageClass
{
	SSC_Invalid = 0xff,

	COFF_SYM_CLASS_END_OF_FUNCTION = -1,  ///< Physical end of function
	COFF_SYM_CLASS_NULL = 0,   ///< No symbol
	COFF_SYM_CLASS_AUTOMATIC = 1,   ///< Stack variable
	COFF_SYM_CLASS_EXTERNAL = 2,   ///< External symbol
	COFF_SYM_CLASS_STATIC = 3,   ///< Static
	COFF_SYM_CLASS_REGISTER = 4,   ///< Register variable
	COFF_SYM_CLASS_EXTERNAL_DEF = 5,   ///< External definition
	COFF_SYM_CLASS_LABEL = 6,   ///< Label
	COFF_SYM_CLASS_UNDEFINED_LABEL = 7,   ///< Undefined label
	COFF_SYM_CLASS_MEMBER_OF_STRUCT = 8,   ///< Member of structure
	COFF_SYM_CLASS_ARGUMENT = 9,   ///< Function argument
	COFF_SYM_CLASS_STRUCT_TAG = 10,  ///< Structure tag
	COFF_SYM_CLASS_MEMBER_OF_UNION = 11,  ///< Member of union
	COFF_SYM_CLASS_UNION_TAG = 12,  ///< Union tag
	COFF_SYM_CLASS_TYPE_DEFINITION = 13,  ///< Type definition
	COFF_SYM_CLASS_UNDEFINED_STATIC = 14,  ///< Undefined static
	COFF_SYM_CLASS_ENUM_TAG = 15,  ///< Enumeration tag
	COFF_SYM_CLASS_MEMBER_OF_ENUM = 16,  ///< Member of enumeration
	COFF_SYM_CLASS_REGISTER_PARAM = 17,  ///< Register parameter
	COFF_SYM_CLASS_BIT_FIELD = 18,  ///< Bit field
	/// ".bb" or ".eb" - beginning or end of block
	COFF_SYM_CLASS_BLOCK = 100,
	/// ".bf" or ".ef" - beginning or end of function
	COFF_SYM_CLASS_FUNCTION = 101,
	COFF_SYM_CLASS_END_OF_STRUCT = 102, ///< End of structure
	COFF_SYM_CLASS_FILE = 103, ///< File name
	/// Line number, reformatted as symbol
	COFF_SYM_CLASS_SECTION = 104,
	COFF_SYM_CLASS_WEAK_EXTERNAL = 105, ///< Duplicate tag
	/// External symbol in dmert public lib
	COFF_SYM_CLASS_CLR_TOKEN = 107
};

enum SymbolBaseType
{
	COFF_SYM_TYPE_NULL = 0,  ///< No type information or unknown base type.
	COFF_SYM_TYPE_VOID = 1,  ///< Used with void pointers and functions.
	COFF_SYM_TYPE_CHAR = 2,  ///< A character (signed byte).
	COFF_SYM_TYPE_SHORT = 3,  ///< A 2-byte signed integer.
	COFF_SYM_TYPE_INT = 4,  ///< A natural integer type on the target.
	COFF_SYM_TYPE_LONG = 5,  ///< A 4-byte signed integer.
	COFF_SYM_TYPE_FLOAT = 6,  ///< A 4-byte floating-point number.
	COFF_SYM_TYPE_DOUBLE = 7,  ///< An 8-byte floating-point number.
	COFF_SYM_TYPE_STRUCT = 8,  ///< A structure.
	COFF_SYM_TYPE_UNION = 9,  ///< An union.
	COFF_SYM_TYPE_ENUM = 10, ///< An enumerated type.
	COFF_SYM_TYPE_MOE = 11, ///< A member of enumeration (a specific value).
	COFF_SYM_TYPE_BYTE = 12, ///< A byte; unsigned 1-byte integer.
	COFF_SYM_TYPE_WORD = 13, ///< A word; unsigned 2-byte integer.
	COFF_SYM_TYPE_UINT = 14, ///< An unsigned integer of natural size.
	COFF_SYM_TYPE_DWORD = 15  ///< An unsigned 4-byte integer.
};

enum SymbolComplexType
{
	COFF_SYM_DTYPE_NULL = 0, ///< No complex type; simple scalar variable.
	COFF_SYM_DTYPE_POINTER = 1, ///< A pointer to base type.
	COFF_SYM_DTYPE_FUNCTION = 2, ///< A function that returns a base type.
	COFF_SYM_DTYPE_ARRAY = 3, ///< An array of base type.

	/// Type is formed as (base + (derived << SCT_COMPLEX_TYPE_SHIFT))
	SCT_COMPLEX_TYPE_SHIFT = 4
};

enum RelocationTypeI386
{
	COFF_REL_I386_ABSOLUTE = 0x0000,
	COFF_REL_I386_DIR16 = 0x0001,
	COFF_REL_I386_REL16 = 0x0002,
	COFF_REL_I386_DIR32 = 0x0006,
	COFF_REL_I386_DIR32NB = 0x0007,
	COFF_REL_I386_SEG12 = 0x0009,
	COFF_REL_I386_SECTION = 0x000A,
	COFF_REL_I386_SECREL = 0x000B,
	COFF_REL_I386_TOKEN = 0x000C,
	COFF_REL_I386_SECREL7 = 0x000D,
	COFF_REL_I386_REL32 = 0x0014
};

enum RelocationTypeAMD64
{
	COFF_REL_AMD64_ABSOLUTE = 0x0000,
	COFF_REL_AMD64_ADDR64 = 0x0001,
	COFF_REL_AMD64_ADDR32 = 0x0002,
	COFF_REL_AMD64_ADDR32NB = 0x0003,
	COFF_REL_AMD64_REL32 = 0x0004,
	COFF_REL_AMD64_REL32_1 = 0x0005,
	COFF_REL_AMD64_REL32_2 = 0x0006,
	COFF_REL_AMD64_REL32_3 = 0x0007,
	COFF_REL_AMD64_REL32_4 = 0x0008,
	COFF_REL_AMD64_REL32_5 = 0x0009,
	COFF_REL_AMD64_SECTION = 0x000A,
	COFF_REL_AMD64_SECREL = 0x000B,
	COFF_REL_AMD64_SECREL7 = 0x000C,
	COFF_REL_AMD64_TOKEN = 0x000D,
	COFF_REL_AMD64_SREL32 = 0x000E,
	COFF_REL_AMD64_PAIR = 0x000F,
	COFF_REL_AMD64_SSPAN32 = 0x0010
};
