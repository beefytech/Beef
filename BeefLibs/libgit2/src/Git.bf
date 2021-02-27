using System.Interop;
using System;

/*
* Copyright (C) the libgit2 contributors. All rights reserved.
*
* This file is part of libgit2, distributed under the GNU GPL v2 with
* a Linking Exception. For full terms see the included COPYING file.
*/

namespace Git
{
	class GitApi
	{
		public struct git_repository;
		public struct git_worktree;
		public struct git_odb;
		public struct git_buf;
		public struct git_reference;
		public struct git_annotated_commit;
		public struct git_config;
		public struct git_refdb;
		public struct git_index;
		public struct git_remote;
		public struct git_object;
		public struct git_refspec;
		public struct git_proxy_options;
		public struct git_transport;
		public struct git_packbuilder;
		public struct git_revwalk;
		public struct git_diff_file;
		public struct git_tree;
		public struct git_tag;

		/** Time in a signature */
		public struct git_time
		{
			int64 time; /**< time in seconds from epoch */
			int offset; /**< timezone offset, in minutes */
			int8 sign; /**< indicator for questionable '-0000' offsets in signature */
		}

		/** Basic type (loose or packed) of any Git object. */
		public enum git_object_t : c_int
		{
			GIT_OBJECT_ANY =      -2, /**< Object can be any of the following */
			GIT_OBJECT_INVALID =  -1, /**< Object is invalid. */
			GIT_OBJECT_COMMIT =    1, /**< A commit object. */
			GIT_OBJECT_TREE =      2, /**< A tree (directory listing) object. */
			GIT_OBJECT_BLOB =      3, /**< A file revision object. */
			GIT_OBJECT_TAG =       4, /**< An annotated tag object. */
			GIT_OBJECT_OFS_DELTA = 6, /**< A delta, base is given by an offset. */
			GIT_OBJECT_REF_DELTA = 7, /**< A delta, base is given by object id. */
		}

		/** An action signature (e.g. for committers, taggers, etc) */
		public struct git_signature
		{
			char8 *name; /**< full name of the author */
			char8 *email; /**< email of the author */
			git_time whenTime; /**< time when the action happened */
		}

		/**
		 * @file git2/repository.h
		 * @brief Git repository management routines
		 * @defgroup git_repository Git repository management routines
		 * @ingroup Git
		 * @{
		 */

		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// tag.h
		////////////////////////////////////////////////////////////////////////////////////////////////////////
		
		/**
		 * Lookup a tag object from the repository.
		 *
		 * @param out pointer to the looked up tag
		 * @param repo the repo to use when locating the tag.
		 * @param id identity of the tag to locate.
		 * @return 0 or an error code
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern int git_tag_lookup(
			out git_tag *outVal, git_repository *repo, git_oid *id);

		/**
		 * Lookup a tag object from the repository,
		 * given a prefix of its identifier (short id).
		 *
		 * @see git_object_lookup_prefix
		 *
		 * @param out pointer to the looked up tag
		 * @param repo the repo to use when locating the tag.
		 * @param id identity of the tag to locate.
		 * @param len the length of the short identifier
		 * @return 0 or an error code
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern int git_tag_lookup_prefix(
			out git_tag *outVal, git_repository *repo, git_oid *id, c_size len);

		/**
		 * Close an open tag
		 *
		 * You can no longer use the git_tag pointer after this call.
		 *
		 * IMPORTANT: You MUST call this method when you are through with a tag to
		 * release memory. Failure to do so will cause a memory leak.
		 *
		 * @param tag the tag to close
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern void git_tag_free(git_tag *tag);

		/**
		 * Get the id of a tag.
		 *
		 * @param tag a previously loaded tag.
		 * @return object identity for the tag.
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern git_oid * git_tag_id(git_tag *tag);

		/**
		 * Get the repository that contains the tag.
		 *
		 * @param tag A previously loaded tag.
		 * @return Repository that contains this tag.
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern git_repository * git_tag_owner(git_tag *tag);

		/**
		 * Get the tagged object of a tag
		 *
		 * This method performs a repository lookup for the
		 * given object and returns it
		 *
		 * @param target_out pointer where to store the target
		 * @param tag a previously loaded tag.
		 * @return 0 or an error code
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern int git_tag_target(git_object **target_out, git_tag *tag);

		/**
		 * Get the OID of the tagged object of a tag
		 *
		 * @param tag a previously loaded tag.
		 * @return pointer to the OID
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern git_oid * git_tag_target_id(git_tag *tag);

		/**
		 * Get the type of a tag's tagged object
		 *
		 * @param tag a previously loaded tag.
		 * @return type of the tagged object
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern git_object_t git_tag_target_type(git_tag *tag);

		/**
		 * Get the name of a tag
		 *
		 * @param tag a previously loaded tag.
		 * @return name of the tag
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern char8 * git_tag_name(git_tag *tag);

		/**
		 * Get the tagger (author) of a tag
		 *
		 * @param tag a previously loaded tag.
		 * @return reference to the tag's author or NULL when unspecified
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern git_signature * git_tag_tagger(git_tag *tag);

		/**
		 * Get the message of a tag
		 *
		 * @param tag a previously loaded tag.
		 * @return message of the tag or NULL when unspecified
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern char8 * git_tag_message(git_tag *tag);

		/**
		 * Create a new tag in the repository from an object
		 *
		 * A new reference will also be created pointing to
		 * this tag object. If `force` is true and a reference
		 * already exists with the given name, it'll be replaced.
		 *
		 * The message will not be cleaned up. This can be achieved
		 * through `git_message_prettify()`.
		 *
		 * The tag name will be checked for validity. You must avoid
		 * the characters '~', '^', ':', '\\', '?', '[', and '*', and the
		 * sequences ".." and "@{" which have special meaning to revparse.
		 *
		 * @param oid Pointer where to store the OID of the
		 * newly created tag. If the tag already exists, this parameter
		 * will be the oid of the existing tag, and the function will
		 * return a GIT_EEXISTS error code.
		 *
		 * @param repo Repository where to store the tag
		 *
		 * @param tag_name Name for the tag; this name is validated
		 * for consistency. It should also not conflict with an
		 * already existing tag name
		 *
		 * @param target Object to which this tag points. This object
		 * must belong to the given `repo`.
		 *
		 * @param tagger Signature of the tagger for this tag, and
		 * of the tagging time
		 *
		 * @param message Full message for this tag
		 *
		 * @param force Overwrite existing references
		 *
		 * @return 0 on success, GIT_EINVALIDSPEC or an error code
		 *	A tag object is written to the ODB, and a proper reference
		 *	is written in the /refs/tags folder, pointing to it
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern int git_tag_create(
			git_oid *oid,
			git_repository *repo,
			char8 *tag_name,
			git_object *target,
			git_signature *tagger,
			char8 *message,
			c_int force);

		/**
		 * Create a new tag in the object database pointing to a git_object
		 *
		 * The message will not be cleaned up. This can be achieved
		 * through `git_message_prettify()`.
		 *
		 * @param oid Pointer where to store the OID of the
		 * newly created tag
		 *
		 * @param repo Repository where to store the tag
		 *
		 * @param tag_name Name for the tag
		 *
		 * @param target Object to which this tag points. This object
		 * must belong to the given `repo`.
		 *
		 * @param tagger Signature of the tagger for this tag, and
		 * of the tagging time
		 *
		 * @param message Full message for this tag
		 *
		 * @return 0 on success or an error code
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern int git_tag_annotation_create(
			git_oid *oid,
			git_repository *repo,
			char8 *tag_name,
			git_object *target,
			git_signature *tagger,
			char8 *message);

		/**
		 * Create a new tag in the repository from a buffer
		 *
		 * @param oid Pointer where to store the OID of the newly created tag
		 * @param repo Repository where to store the tag
		 * @param buffer Raw tag data
		 * @param force Overwrite existing tags
		 * @return 0 on success; error code otherwise
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern int git_tag_create_from_buffer(
			git_oid *oid,
			git_repository *repo,
			char8 *buffer,
			c_int force);

		/**
		 * Create a new lightweight tag pointing at a target object
		 *
		 * A new direct reference will be created pointing to
		 * this target object. If `force` is true and a reference
		 * already exists with the given name, it'll be replaced.
		 *
		 * The tag name will be checked for validity.
		 * See `git_tag_create()` for rules about valid names.
		 *
		 * @param oid Pointer where to store the OID of the provided
		 * target object. If the tag already exists, this parameter
		 * will be filled with the oid of the existing pointed object
		 * and the function will return a GIT_EEXISTS error code.
		 *
		 * @param repo Repository where to store the lightweight tag
		 *
		 * @param tag_name Name for the tag; this name is validated
		 * for consistency. It should also not conflict with an
		 * already existing tag name
		 *
		 * @param target Object to which this tag points. This object
		 * must belong to the given `repo`.
		 *
		 * @param force Overwrite existing references
		 *
		 * @return 0 on success, GIT_EINVALIDSPEC or an error code
		 *	A proper reference is written in the /refs/tags folder,
		 * pointing to the provided target object
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern int git_tag_create_lightweight(
			git_oid *oid,
			git_repository *repo,
			char8 *tag_name,
			git_object *target,
			c_int force);

		/**
		 * Delete an existing tag reference.
		 *
		 * The tag name will be checked for validity.
		 * See `git_tag_create()` for rules about valid names.
		 *
		 * @param repo Repository where lives the tag
		 *
		 * @param tag_name Name of the tag to be deleted;
		 * this name is validated for consistency.
		 *
		 * @return 0 on success, GIT_EINVALIDSPEC or an error code
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern int git_tag_delete(
			git_repository *repo,
			char8 *tag_name);

		/**
		 * Fill a list with all the tags in the Repository
		 *
		 * The string array will be filled with the names of the
		 * matching tags; these values are owned by the user and
		 * should be free'd manually when no longer needed, using
		 * `git_strarray_free`.
		 *
		 * @param tag_names Pointer to a git_strarray structure where
		 *		the tag names will be stored
		 * @param repo Repository where to find the tags
		 * @return 0 or an error code
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern int git_tag_list(
			git_strarray *tag_names,
			git_repository *repo);

		/**
		 * Fill a list with all the tags in the Repository
		 * which name match a defined pattern
		 *
		 * If an empty pattern is provided, all the tags
		 * will be returned.
		 *
		 * The string array will be filled with the names of the
		 * matching tags; these values are owned by the user and
		 * should be free'd manually when no longer needed, using
		 * `git_strarray_free`.
		 *
		 * @param tag_names Pointer to a git_strarray structure where
		 *		the tag names will be stored
		 * @param pattern Standard fnmatch pattern
		 * @param repo Repository where to find the tags
		 * @return 0 or an error code
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern int git_tag_list_match(
			git_strarray *tag_names,
			char8 *pattern,
			git_repository *repo);

		/**
		 * Callback used to iterate over tag names
		 *
		 * @see git_tag_foreach
		 *
		 * @param name The tag name
		 * @param oid The tag's OID
		 * @param payload Payload passed to git_tag_foreach
		 * @return non-zero to terminate the iteration
		 */
		public function c_int git_tag_foreach_cb(char8 *name, git_oid *oid, void *payload);

		/**
		 * Call callback `cb' for each tag in the repository
		 *
		 * @param repo Repository
		 * @param callback Callback function
		 * @param payload Pointer to callback data (optional)
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern c_int git_tag_foreach(
			git_repository *repo,
			git_tag_foreach_cb callback,
			void *payload);


		/**
		 * Recursively peel a tag until a non tag git_object is found
		 *
		 * The retrieved `tag_target` object is owned by the repository
		 * and should be closed with the `git_object_free` method.
		 *
		 * @param tag_target_out Pointer to the peeled git_object
		 * @param tag The tag to be processed
		 * @return 0 or an error code
		 */
		public static extern c_int git_tag_peel(
			git_object **tag_target_out,
			git_tag *tag);

		/**
		 * Create an in-memory copy of a tag. The copy must be explicitly
		 * free'd or it will leak.
		 *
		 * @param out Pointer to store the copy of the tag
		 * @param source Original tag to copy
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern c_int git_tag_dup(out git_tag *outVal, git_tag *source);

		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// global.h
		////////////////////////////////////////////////////////////////////////////////////////////////////////

		/**
		 * Init the global state
		 *
		 * This function must be called before any other libgit2 function in
		 * order to set up global state and threading.
		 *
		 * This function may be called multiple times - it will return the number
		 * of times the initialization has been called (including this one) that have
		 * not subsequently been shutdown.
		 *
		 * @return the number of initializations of the library, or an error code.
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern c_int git_libgit2_init();

		/**
		 * Shutdown the global state
		 *
		 * Clean up the global state and threading context after calling it as
		 * many times as `git_libgit2_init()` was called - it will return the
		 * number of remainining initializations that have not been shutdown
		 * (after this one).
		 * 
		 * @return the number of remaining initializations of the library, or an
		 * error code.
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern c_int git_libgit2_shutdown();

		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// pack.h
		////////////////////////////////////////////////////////////////////////////////////////////////////////

		/**
		 * Stages that are reported by the packbuilder progress callback.
		 */
		public enum git_packbuilder_stage_t : c_int
		{
			GIT_PACKBUILDER_ADDING_OBJECTS = 0,
			GIT_PACKBUILDER_DELTAFICATION = 1,
		}

		/**
		 * Initialize a new packbuilder
		 *
		 * @param out The new packbuilder object
		 * @param repo The repository
		 *
		 * @return 0 or an error code
		 */
		public static extern int git_packbuilder_new(git_packbuilder **outVal, git_repository *repo);

		/**
		 * Set number of threads to spawn
		 *
		 * By default, libgit2 won't spawn any threads at all;
		 * when set to 0, libgit2 will autodetect the number of
		 * CPUs.
		 *
		 * @param pb The packbuilder
		 * @param n Number of threads to spawn
		 * @return number of actual threads to be used
		 */
		public static extern c_uint git_packbuilder_set_threads(git_packbuilder *pb, c_uint n);

		/**
		 * Insert a single object
		 *
		 * For an optimal pack it's mandatory to insert objects in recency order,
		 * commits followed by trees and blobs.
		 *
		 * @param pb The packbuilder
		 * @param id The oid of the commit
		 * @param name The name; might be NULL
		 *
		 * @return 0 or an error code
		 */
		public static extern int git_packbuilder_insert(git_packbuilder *pb, git_oid *id, char8 *name);

		/**
		 * Insert a root tree object
		 *
		 * This will add the tree as well as all referenced trees and blobs.
		 *
		 * @param pb The packbuilder
		 * @param id The oid of the root tree
		 *
		 * @return 0 or an error code
		 */
		public static extern int git_packbuilder_insert_tree(git_packbuilder *pb, git_oid *id);

		/**
		 * Insert a commit object
		 *
		 * This will add a commit as well as the completed referenced tree.
		 *
		 * @param pb The packbuilder
		 * @param id The oid of the commit
		 *
		 * @return 0 or an error code
		 */
		public static extern int git_packbuilder_insert_commit(git_packbuilder *pb, git_oid *id);

		/**
		 * Insert objects as given by the walk
		 *
		 * Those commits and all objects they reference will be inserted into
		 * the packbuilder.
		 *
		 * @param pb the packbuilder
		 * @param walk the revwalk to use to fill the packbuilder
		 *
		 * @return 0 or an error code
		 */
		public static extern int git_packbuilder_insert_walk(git_packbuilder *pb, git_revwalk *walk);

		/**
		 * Recursively insert an object and its referenced objects
		 *
		 * Insert the object as well as any object it references.
		 *
		 * @param pb the packbuilder
		 * @param id the id of the root object to insert
		 * @param name optional name for the object
		 * @return 0 or an error code
		 */
		public static extern int git_packbuilder_insert_recur(git_packbuilder *pb, git_oid *id, char8 *name);

		/**
		 * Write the contents of the packfile to an in-memory buffer
		 *
		 * The contents of the buffer will become a valid packfile, even though there
		 * will be no attached index
		 *
		 * @param buf Buffer where to write the packfile
		 * @param pb The packbuilder
		 */
		public static extern int git_packbuilder_write_buf(git_buf *buf, git_packbuilder *pb);

		/**
		 * Write the new pack and corresponding index file to path.
		 *
		 * @param pb The packbuilder
		 * @param path Path to the directory where the packfile and index should be stored, or NULL for default location
		 * @param mode permissions to use creating a packfile or 0 for defaults
		 * @param progress_cb function to call with progress information from the indexer (optional)
		 * @param progress_cb_payload payload for the progress callback (optional)
		 *
		 * @return 0 or an error code
		 */
		public static extern int git_packbuilder_write(
			git_packbuilder *pb,
			char8 *path,
			c_uint mode,
			git_indexer_progress_cb progress_cb,
			void *progress_cb_payload);

		/**
		* Get the packfile's hash
		*
		* A packfile's name is derived from the sorted hashing of all object
		* names. This is only correct after the packfile has been written.
		*
		* @param pb The packbuilder object
		*/
		public static extern git_oid * git_packbuilder_hash(git_packbuilder *pb);

		/**
		 * Callback used to iterate over packed objects
		 *
		 * @see git_packbuilder_foreach
		 *
		 * @param buf A pointer to the object's data
		 * @param size The size of the underlying object
		 * @param payload Payload passed to git_packbuilder_foreach
		 * @return non-zero to terminate the iteration
		 */
		public function c_int git_packbuilder_foreach_cb(void *buf, c_size size, void *payload);

		/**
		 * Create the new pack and pass each object to the callback
		 *
		 * @param pb the packbuilder
		 * @param cb the callback to call with each packed object's buffer
		 * @param payload the callback's data
		 * @return 0 or an error code
		 */
		public static extern int git_packbuilder_foreach(git_packbuilder *pb, git_packbuilder_foreach_cb cb, void *payload);

		/**
		 * Get the total number of objects the packbuilder will write out
		 *
		 * @param pb the packbuilder
		 * @return the number of objects in the packfile
		 */
		public static extern c_size git_packbuilder_object_count(git_packbuilder *pb);

		/**
		 * Get the number of objects the packbuilder has already written out
		 *
		 * @param pb the packbuilder
		 * @return the number of objects which have already been written
		 */
		public static extern c_size git_packbuilder_written(git_packbuilder *pb);

		/** Packbuilder progress notification function */
		public function c_int git_packbuilder_progress(
			int stage,
			uint32 current,
			uint32 total,
			void *payload);

		/**
		 * Set the callbacks for a packbuilder
		 *
		 * @param pb The packbuilder object
		 * @param progress_cb Function to call with progress information during
		 * pack building. Be aware that this is called inline with pack building
		 * operations, so performance may be affected.
		 * @param progress_cb_payload Payload for progress callback.
		 * @return 0 or an error code
		 */
		public static extern int git_packbuilder_set_callbacks(
			git_packbuilder *pb,
			git_packbuilder_progress progress_cb,
			void *progress_cb_payload);

		/**
		 * Free the packbuilder and all associated data
		 *
		 * @param pb The packbuilder
		 */
		public static extern void git_packbuilder_free(git_packbuilder *pb);

		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// oid.h
		////////////////////////////////////////////////////////////////////////////////////////////////////////
		
		/** Size (in bytes) of a raw/binary oid */
		const int GIT_OID_RAWSZ = 20;

		/** Size (in bytes) of a hex formatted oid */
		const int GIT_OID_HEXSZ = (GIT_OID_RAWSZ * 2);

		/** Minimum length (in number of hex characters,
		 * i.e. packets of 4 bits) of an oid prefix */
		const int GIT_OID_MINPREFIXLEN = 4;

		/** Unique identity of any object (commit, tree, blob, tag). */
		public struct git_oid
		{
			/** raw binary formatted id */
			public uint8[GIT_OID_RAWSZ] id;
		}

		/**
		 * Parse a hex formatted object id into a git_oid.
		 *
		 * @param out oid structure the result is written into.
		 * @param str input hex string; must be pointing at the start of
		 *		the hex sequence and have at least the number of bytes
		 *		needed for an oid encoded in hex (40 bytes).
		 * @return 0 or an error code
		 */
		public static extern int git_oid_fromstr(git_oid *outVal, char8 *str);

		/**
		 * Parse a hex formatted null-terminated string into a git_oid.
		 *
		 * @param out oid structure the result is written into.
		 * @param str input hex string; must be null-terminated.
		 * @return 0 or an error code
		 */
		public static extern int git_oid_fromstrp(git_oid *outVal, char8 *str);

		/**
		 * Parse N characters of a hex formatted object id into a git_oid.
		 *
		 * If N is odd, the last byte's high nibble will be read in and the
		 * low nibble set to zero.
		 *
		 * @param out oid structure the result is written into.
		 * @param str input hex string of at least size `length`
		 * @param length length of the input string
		 * @return 0 or an error code
		 */
		public static extern int git_oid_fromstrn(git_oid *outVal, char8 *str, c_size length);

		/**
		 * Copy an already raw oid into a git_oid structure.
		 *
		 * @param out oid structure the result is written into.
		 * @param raw the raw input bytes to be copied.
		 * @return 0 on success or error code
		 */
		public static extern int git_oid_fromraw(git_oid *outVal, uint8 *raw);

		/**
		 * Format a git_oid into a hex string.
		 *
		 * @param out output hex string; must be pointing at the start of
		 *		the hex sequence and have at least the number of bytes
		 *		needed for an oid encoded in hex (40 bytes). Only the
		 *		oid digits are written; a '\\0' terminator must be added
		 *		by the caller if it is required.
		 * @param id oid structure to format.
		 * @return 0 on success or error code
		 */
		public static extern int git_oid_fmt(char8 *outVal, git_oid *id);

		/**
		 * Format a git_oid into a partial hex string.
		 *
		 * @param out output hex string; you say how many bytes to write.
		 *		If the number of bytes is > GIT_OID_HEXSZ, extra bytes
		 *		will be zeroed; if not, a '\0' terminator is NOT added.
		 * @param n number of characters to write into out string
		 * @param id oid structure to format.
		 * @return 0 on success or error code
		 */
		public static extern int git_oid_nfmt(char8 *outVal, c_size n, git_oid *id);

		/**
		 * Format a git_oid into a loose-object path string.
		 *
		 * The resulting string is "aa/...", where "aa" is the first two
		 * hex digits of the oid and "..." is the remaining 38 digits.
		 *
		 * @param out output hex string; must be pointing at the start of
		 *		the hex sequence and have at least the number of bytes
		 *		needed for an oid encoded in hex (41 bytes). Only the
		 *		oid digits are written; a '\\0' terminator must be added
		 *		by the caller if it is required.
		 * @param id oid structure to format.
		 * @return 0 on success, non-zero callback return value, or error code
		 */
		public static extern int git_oid_pathfmt(char8 *outVal, git_oid *id);

		/**
		 * Format a git_oid into a statically allocated c-string.
		 *
		 * The c-string is owned by the library and should not be freed
		 * by the user. If libgit2 is built with thread support, the string
		 * will be stored in TLS (i.e. one buffer per thread) to allow for
		 * concurrent calls of the function.
		 *
		 * @param oid The oid structure to format
		 * @return the c-string
		 */
		public static extern char8 * git_oid_tostr_s(git_oid *oid);

		/**
		 * Format a git_oid into a buffer as a hex format c-string.
		 *
		 * If the buffer is smaller than GIT_OID_HEXSZ+1, then the resulting
		 * oid c-string will be truncated to n-1 characters (but will still be
		 * NUL-byte terminated).
		 *
		 * If there are any input parameter errors (out == NULL, n == 0, oid ==
		 * NULL), then a pointer to an empty string is returned, so that the
		 * return value can always be printed.
		 *
		 * @param out the buffer into which the oid string is output.
		 * @param n the size of the out buffer.
		 * @param id the oid structure to format.
		 * @return the out buffer pointer, assuming no input parameter
		 *			errors, otherwise a pointer to an empty string.
		 */
		public static extern char8 * git_oid_tostr(char8 *outVal, c_size n, git_oid *id);

		/**
		 * Copy an oid from one structure to another.
		 *
		 * @param out oid structure the result is written into.
		 * @param src oid structure to copy from.
		 * @return 0 on success or error code
		 */
		public static extern int git_oid_cpy(git_oid *outVal, git_oid *src);

		/**
		 * Compare two oid structures.
		 *
		 * @param a first oid structure.
		 * @param b second oid structure.
		 * @return <0, 0, >0 if a < b, a == b, a > b.
		 */
		public static extern int git_oid_cmp(git_oid *a, git_oid *b);

		/**
		 * Compare two oid structures for equality
		 *
		 * @param a first oid structure.
		 * @param b second oid structure.
		 * @return true if equal, false otherwise
		 */
		public static extern int git_oid_equal(git_oid *a, git_oid *b);

		/**
		 * Compare the first 'len' hexadecimal characters (packets of 4 bits)
		 * of two oid structures.
		 *
		 * @param a first oid structure.
		 * @param b second oid structure.
		 * @param len the number of hex chars to compare
		 * @return 0 in case of a match
		 */
		public static extern int git_oid_ncmp(git_oid *a, git_oid *b, c_size len);

		/**
		 * Check if an oid equals an hex formatted object id.
		 *
		 * @param id oid structure.
		 * @param str input hex string of an object id.
		 * @return 0 in case of a match, -1 otherwise.
		 */
		public static extern int git_oid_streq(git_oid *id, char8 *str);

		/**
		 * Compare an oid to an hex formatted object id.
		 *
		 * @param id oid structure.
		 * @param str input hex string of an object id.
		 * @return -1 if str is not valid, <0 if id sorts before str,
		 *         0 if id matches str, >0 if id sorts after str.
		 */
		public static extern int git_oid_strcmp(git_oid *id, char8 *str);

		/**
		 * Check is an oid is all zeros.
		 *
		 * @return 1 if all zeros, 0 otherwise.
		 */
		public static extern int git_oid_is_zero(git_oid *id);

		/**
		 * OID Shortener object
		 */
		public struct git_oid_shorten;

		/**
		 * Create a new OID shortener.
		 *
		 * The OID shortener is used to process a list of OIDs
		 * in text form and return the shortest length that would
		 * uniquely identify all of them.
		 *
		 * E.g. look at the result of `git log --abbrev`.
		 *
		 * @param min_length The minimal length for all identifiers,
		 *		which will be used even if shorter OIDs would still
		 *		be unique.
		 *	@return a `git_oid_shorten` instance, NULL if OOM
		 */
		public static extern git_oid_shorten * git_oid_shorten_new(c_size min_length);

		/**
		 * Add a new OID to set of shortened OIDs and calculate
		 * the minimal length to uniquely identify all the OIDs in
		 * the set.
		 *
		 * The OID is expected to be a 40-char hexadecimal string.
		 * The OID is owned by the user and will not be modified
		 * or freed.
		 *
		 * For performance reasons, there is a hard-limit of how many
		 * OIDs can be added to a single set (around ~32000, assuming
		 * a mostly randomized distribution), which should be enough
		 * for any kind of program, and keeps the algorithm fast and
		 * memory-efficient.
		 *
		 * Attempting to add more than those OIDs will result in a
		 * GIT_ERROR_INVALID error
		 *
		 * @param os a `git_oid_shorten` instance
		 * @param text_id an OID in text form
		 * @return the minimal length to uniquely identify all OIDs
		 *		added so far to the set; or an error code (<0) if an
		 *		error occurs.
		 */
		public static extern int git_oid_shorten_add(git_oid_shorten *os, char8 *text_id);

		/**
		 * Free an OID shortener instance
		 *
		 * @param os a `git_oid_shorten` instance
		 */
		public static extern void git_oid_shorten_free(git_oid_shorten *os);


		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// indexer.h
		////////////////////////////////////////////////////////////////////////////////////////////////////////
		
		/** A git indexer object */
		public struct git_indexer;

		/**
		 * This structure is used to provide callers information about the
		 * progress of indexing a packfile, either directly or part of a
		 * fetch or clone that downloads a packfile.
		 */
		public struct git_indexer_progress
		{
			/** number of objects in the packfile being indexed */
			public c_uint total_objects;

			/** received objects that have been hashed */
			public c_uint indexed_objects;

			/** received_objects: objects which have been downloaded */
			public c_uint received_objects;

			/**
			 * locally-available objects that have been injected in order
			 * to fix a thin pack
			 */
			public c_uint local_objects;

			/** number of deltas in the packfile being indexed */
			public c_uint total_deltas;

			/** received deltas that have been indexed */
			public c_uint indexed_deltas;

			/** size of the packfile received up to now */
			public c_size received_bytes;
		}

		/**
		 * Type for progress callbacks during indexing.  Return a value less
		 * than zero to cancel the indexing or download.
		 *
		 * @param stats Structure containing information about the state of the tran    sfer
		 * @param payload Payload provided by caller
		 */
		public function int git_indexer_progress_cb(git_indexer_progress *stats, void *payload);

		/**
		 * Options for indexer configuration
		 */
		public struct git_indexer_options
		{
			c_uint version;

			/** progress_cb function to call with progress information */
			git_indexer_progress_cb progress_cb;
			/** progress_cb_payload payload for the progress callback */
			void *progress_cb_payload;

			/** Do connectivity checks for the received pack */
			uint8 verify;
		}

		//#define GIT_INDEXER_OPTIONS_VERSION 1
		//#define GIT_INDEXER_OPTIONS_INIT { GIT_INDEXER_OPTIONS_VERSION }

		/**
		 * Initializes a `git_indexer_options` with default values. Equivalent to
		 * creating an instance with GIT_INDEXER_OPTIONS_INIT.
		 *
		 * @param opts the `git_indexer_options` struct to initialize.
		 * @param version Version of struct; pass `GIT_INDEXER_OPTIONS_VERSION`
		 * @return Zero on success; -1 on failure.
		 */
		public static extern int git_indexer_options_init(
			git_indexer_options *opts,
			c_uint version);

		/**
		 * Create a new indexer instance
		 *
		 * @param out where to store the indexer instance
		 * @param path to the directory where the packfile should be stored
		 * @param mode permissions to use creating packfile or 0 for defaults
		 * @param odb object database from which to read base objects when
		 * fixing thin packs. Pass NULL if no thin pack is expected (an error
		 * will be returned if there are bases missing)
		 * @param opts Optional structure containing additional options. See
		 * `git_indexer_options` above.
		 */
		public static extern int git_indexer_new(
				git_indexer **outVal,
				char8 *path,
				c_uint mode,
				git_odb *odb,
				git_indexer_options *opts);

		/**
		 * Add data to the indexer
		 *
		 * @param idx the indexer
		 * @param data the data to add
		 * @param size the size of the data in bytes
		 * @param stats stat storage
		 */
		public static extern int git_indexer_append(git_indexer *idx, void *data, c_size size, git_indexer_progress *stats);

		/**
		 * Finalize the pack and index
		 *
		 * Resolve any pending deltas and write out the index file
		 *
		 * @param idx the indexer
		 */
		public static extern int git_indexer_commit(git_indexer *idx, git_indexer_progress *stats);

		/**
		 * Get the packfile's hash
		 *
		 * A packfile's name is derived from the sorted hashing of all object
		 * names. This is only correct after the index has been finalized.
		 *
		 * @param idx the indexer instance
		 */
		public static extern git_oid * git_indexer_hash(git_indexer *idx);

		/**
		 * Free the indexer and its resources
		 *
		 * @param idx the indexer to free
		 */
		public static extern void git_indexer_free(git_indexer *idx);

		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// cert.h
		////////////////////////////////////////////////////////////////////////////////////////////////////////

		/**
		 * Type of host certificate structure that is passed to the check callback
		 */
		public enum git_cert_t : c_int {
			/**
			 * No information about the certificate is available. This may
			 * happen when using curl.
			 */
			GIT_CERT_NONE,
			/**
			 * The `data` argument to the callback will be a pointer to
			 * the DER-encoded data.
			 */
			GIT_CERT_X509,
			/**
			 * The `data` argument to the callback will be a pointer to a
			 * `git_cert_hostkey` structure.
			 */
			GIT_CERT_HOSTKEY_LIBSSH2,
			/**
			 * The `data` argument to the callback will be a pointer to a
			 * `git_strarray` with `name:content` strings containing
			 * information about the certificate. This is used when using
			 * curl.
			 */
			GIT_CERT_STRARRAY,
		}

		/**
		 * Parent type for `git_cert_hostkey` and `git_cert_x509`.
		 */
		public struct git_cert {
			/**
			 * Type of certificate. A `GIT_CERT_` value.
			 */
			public git_cert_t cert_type;
		};

		/**
		 * Callback for the user's custom certificate checks.
		 *
		 * @param cert The host certificate
		 * @param valid Whether the libgit2 checks (OpenSSL or WinHTTP) think
		 * this certificate is valid
		 * @param host Hostname of the host libgit2 connected to
		 * @param payload Payload provided by the caller
		 * @return 0 to proceed with the connection, < 0 to fail the connection
		 *         or > 0 to indicate that the callback refused to act and that
		 *         the existing validity determination should be honored
		 */
		public function c_int git_transport_certificate_check_cb(git_cert *cert, int valid, char8 *host, void *payload);

		/**
		 * Type of SSH host fingerprint
		 */
		public enum git_cert_ssh_t : c_int {
			/** MD5 is available */
			GIT_CERT_SSH_MD5 = (1 << 0),
			/** SHA-1 is available */
			GIT_CERT_SSH_SHA1 = (1 << 1),
			/** SHA-256 is available */
			GIT_CERT_SSH_SHA256 = (1 << 2),
		}

		/**
		 * Hostkey information taken from libssh2
		 */
		public struct git_cert_hostkey
		{
			public git_cert parent; /**< The parent cert */

			/**
			 * A hostkey type from libssh2, either
			 * `GIT_CERT_SSH_MD5` or `GIT_CERT_SSH_SHA1`
			 */
			public git_cert_ssh_t type;

			/**
			 * Hostkey hash. If type has `GIT_CERT_SSH_MD5` set, this will
			 * have the MD5 hash of the hostkey.
			 */
			public uint8[16] hash_md5;

			/**
			 * Hostkey hash. If type has `GIT_CERT_SSH_SHA1` set, this will
			 * have the SHA-1 hash of the hostkey.
			 */
			public uint8[20] hash_sha1;

			/**
			 * Hostkey hash. If type has `GIT_CERT_SSH_SHA256` set, this will
			 * have the SHA-256 hash of the hostkey.
			 */
			public uint8[32] hash_sha256;
		}

		/**
		 * X.509 certificate information
		 */
		public struct git_cert_x509
		{
			git_cert parent; /**< The parent cert */

			/**
			 * Pointer to the X.509 certificate data
			 */
			void *data;

			/**
			 * Length of the memory block pointed to by `data`.
			 */
			c_size len;
		}


		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// credential.h
		////////////////////////////////////////////////////////////////////////////////////////////////////////

		public enum git_credential_t : c_int
		{
			/**
			 * A vanilla user/password request
			 * @see git_credential_userpass_plaintext_new
			 */
			GIT_CREDENTIAL_USERPASS_PLAINTEXT = (1u << 0),

			/**
			 * An SSH key-based authentication request
			 * @see git_credential_ssh_key_new
			 */
			GIT_CREDENTIAL_SSH_KEY = (1u << 1),

			/**
			 * An SSH key-based authentication request, with a custom signature
			 * @see git_credential_ssh_custom_new
			 */
			GIT_CREDENTIAL_SSH_CUSTOM = (1u << 2),

			/**
			 * An NTLM/Negotiate-based authentication request.
			 * @see git_credential_default
			 */
			GIT_CREDENTIAL_DEFAULT = (1u << 3),

			/**
			 * An SSH interactive authentication request
			 * @see git_credential_ssh_interactive_new
			 */
			GIT_CREDENTIAL_SSH_INTERACTIVE = (1u << 4),

			/**
			 * Username-only authentication request
			 *
			 * Used as a pre-authentication step if the underlying transport
			 * (eg. SSH, with no username in its URL) does not know which username
			 * to use.
			 *
			 * @see git_credential_username_new
			 */
			GIT_CREDENTIAL_USERNAME = (1u << 5),

			/**
			 * An SSH key-based authentication request
			 *
			 * Allows credentials to be read from memory instead of files.
			 * Note that because of differences in crypto backend support, it might
			 * not be functional.
			 *
			 * @see git_credential_ssh_key_memory_new
			 */
			GIT_CREDENTIAL_SSH_MEMORY = (1u << 6),
		}

		/**
		 * The base structure for all credential types
		 */
		public struct git_credential;

		public struct git_credential_userpass_plaintext;

		/** Username-only credential information */
		public struct ggit_credential_username;

		/** A key for NTLM/Kerberos "default" credentials */
		public  struct git_credential_default;

		/**
		 * A ssh key from disk
		 */
		public  struct git_credential_ssh_key;

		/**
		 * Keyboard-interactive based ssh authentication
		 */
		public  struct git_credential_ssh_interactive;

		/**
		 * A key with a custom signature function
		 */
		public struct git_credential_ssh_custom;

		/**
		 * Credential acquisition callback.
		 *
		 * This callback is usually involved any time another system might need
		 * authentication. As such, you are expected to provide a valid
		 * git_credential object back, depending on allowed_types (a
		 * git_credential_t bitmask).
		 *
		 * Note that most authentication details are your responsibility - this
		 * callback will be called until the authentication succeeds, or you report
		 * an error. As such, it's easy to get in a loop if you fail to stop providing
		 * the same incorrect credentials.
		 *
		 * @param out The newly created credential object.
		 * @param url The resource for which we are demanding a credential.
		 * @param username_from_url The username that was embedded in a "user\@host"
		 *                          remote url, or NULL if not included.
		 * @param allowed_types A bitmask stating which credential types are OK to return.
		 * @param payload The payload provided when specifying this callback.
		 * @return 0 for success, < 0 to indicate an error, > 0 to indicate
		 *       no credential was acquired
		 */
		public function c_int git_credential_acquire_cb(
			git_credential **outVal,
			char8 *url,
			char8 *username_from_url,
			c_uint allowed_types,
			void *payload);

		/**
		 * Free a credential.
		 *
		 * This is only necessary if you own the object; that is, if you are a
		 * transport.
		 *
		 * @param cred the object to free
		 */
		public static extern void git_credential_free(git_credential *cred);

		/**
		 * Check whether a credential object contains username information.
		 *
		 * @param cred object to check
		 * @return 1 if the credential object has non-NULL username, 0 otherwise
		 */
		public static extern int git_credential_has_username(git_credential *cred);

		/**
		 * Return the username associated with a credential object.
		 *
		 * @param cred object to check
		 * @return the credential username, or NULL if not applicable
		 */
		public static extern char8* git_credential_get_username(git_credential *cred);

		/**
		 * Create a new plain-text username and password credential object.
		 * The supplied credential parameter will be internally duplicated.
		 *
		 * @param out The newly created credential object.
		 * @param username The username of the credential.
		 * @param password The password of the credential.
		 * @return 0 for success or an error code for failure
		 */
		public static extern int git_credential_userpass_plaintext_new(
			git_credential **outVal,
			char8 *username,
			char8 *password);

		/**
		 * Create a "default" credential usable for Negotiate mechanisms like NTLM
		 * or Kerberos authentication.
		 *
		 * @param out The newly created credential object.
		 * @return 0 for success or an error code for failure
		 */
		public static extern int git_credential_default_new(git_credential **outVal);

		/**
		 * Create a credential to specify a username.
		 *
		 * This is used with ssh authentication to query for the username if
		 * none is specified in the url.
		 *
		 * @param out The newly created credential object.
		 * @param username The username to authenticate with
		 * @return 0 for success or an error code for failure
		 */
		public static extern int git_credential_username_new(git_credential **outVal, char8 *username);

		/**
		 * Create a new passphrase-protected ssh key credential object.
		 * The supplied credential parameter will be internally duplicated.
		 *
		 * @param out The newly created credential object.
		 * @param username username to use to authenticate
		 * @param publickey The path to the public key of the credential.
		 * @param privatekey The path to the private key of the credential.
		 * @param passphrase The passphrase of the credential.
		 * @return 0 for success or an error code for failure
		 */
		public static extern int git_credential_ssh_key_new(
			git_credential **outVal,
			char8* username,
			char8* publickey,
			char8* privatekey,
			char8* passphrase);

		/**
		 * Create a new ssh key credential object reading the keys from memory.
		 *
		 * @param out The newly created credential object.
		 * @param username username to use to authenticate.
		 * @param publickey The public key of the credential.
		 * @param privatekey The private key of the credential.
		 * @param passphrase The passphrase of the credential.
		 * @return 0 for success or an error code for failure
		 */
		public static extern int git_credential_ssh_key_memory_new(
			git_credential **outVal,
			char8* username,
			char8* publickey,
			char8* privatekey,
			char8* passphrase);

		/*
		 * If the user hasn't included libssh2.h before git2.h, we need to
		 * define a few types for the callback signatures.
		 */
//#ifndef LIBSSH2_VERSION
		public struct LIBSSH2_SESSION;
		public struct LIBSSH2_USERAUTH_KBDINT_PROMPT;
		public struct LIBSSH2_USERAUTH_KBDINT_RESPONSE;
//#endif

		public function void git_credential_ssh_interactive_cb(
			char8* name,
			int name_len,
			char8* instruction, int instruction_len,
			int num_prompts, LIBSSH2_USERAUTH_KBDINT_PROMPT *prompts,
			LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses,
			void **abstractVal);


		/**
		 * Create a new ssh keyboard-interactive based credential object.
		 * The supplied credential parameter will be internally duplicated.
		 *
		 * @param username Username to use to authenticate.
		 * @param prompt_callback The callback method used for prompts.
		 * @param payload Additional data to pass to the callback.
		 * @return 0 for success or an error code for failure.
		 */
		public static extern c_int git_credential_ssh_interactive_new(
			git_credential **outVal,
			char8* username,
			git_credential_ssh_interactive_cb prompt_callback,
			void *payload);

		/**
		 * Create a new ssh key credential object used for querying an ssh-agent.
		 * The supplied credential parameter will be internally duplicated.
		 *
		 * @param out The newly created credential object.
		 * @param username username to use to authenticate
		 * @return 0 for success or an error code for failure
		 */
		public static extern c_int git_credential_ssh_key_from_agent(
			git_credential **outVal,
			char8* username);

		public function c_int git_credential_sign_cb(
			LIBSSH2_SESSION *session,
			uint8 **sig, c_size *sig_len,
			uint8 *data, c_size data_len,
			void **abstractVal);

		/**
		 * Create an ssh key credential with a custom signing function.
		 *
		 * This lets you use your own function to sign the challenge.
		 *
		 * This function and its credential type is provided for completeness
		 * and wraps `libssh2_userauth_publickey()`, which is undocumented.
		 *
		 * The supplied credential parameter will be internally duplicated.
		 *
		 * @param out The newly created credential object.
		 * @param username username to use to authenticate
		 * @param publickey The bytes of the public key.
		 * @param publickey_len The length of the public key in bytes.
		 * @param sign_callback The callback method to sign the data during the challenge.
		 * @param payload Additional data to pass to the callback.
		 * @return 0 for success or an error code for failure
		 */
		public static extern int git_credential_ssh_custom_new(
			git_credential **outVal,
			char8* username,
			char8* publickey,
			c_size publickey_len,
			git_credential_sign_cb sign_callback,
			void *payload);


		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// transport.h
		////////////////////////////////////////////////////////////////////////////////////////////////////////

		public function c_int git_transport_message_cb(char8 *str, int len, void *payload);

		/** Signature of a function which creates a transport */
		public function c_int git_transport_cb(git_transport **outVal, git_remote *owner, void *param);

		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// net.h
		////////////////////////////////////////////////////////////////////////////////////////////////////////

		enum git_direction : c_int
		{
			GIT_DIRECTION_FETCH = 0,
			GIT_DIRECTION_PUSH  = 1
		}

		/**
		 * Description of a reference advertised by a remote server, given out
		 * on `ls` calls.
		 */
		[CRepr]
		struct git_remote_head
		{
			public c_int local; /* available locally */
			public git_oid oid;
			public git_oid loid;
			public char8 *name;
			/**
			 * If the server send a symref mapping for this ref, this will
			 * point to the target.
			 */
			public char8* symref_target;
		};

		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// strarray.h
		////////////////////////////////////////////////////////////////////////////////////////////////////////

		/** Array of strings */
		public struct git_strarray
		{
			char8 **strings;
			c_size count;
		}

		/**
		 * Free the strings contained in a string array.  This method should
		 * be called on `git_strarray` objects that were provided by the
		 * library.  Not doing so, will result in a memory leak.
		 *
		 * This does not free the `git_strarray` itself, since the library will
		 * never allocate that object directly itself.
		 *
		 * @param array The git_strarray that contains strings to free
		 */
		public static extern void git_strarray_dispose(git_strarray *array);

		/**
		 * Copy a string array object from source to target.
		 *
		 * Note: target is overwritten and hence should be empty, otherwise its
		 * contents are leaked.  Call git_strarray_free() if necessary.
		 *
		 * @param tgt target
		 * @param src source
		 * @return 0 on success, < 0 on allocation failure
		 */
		public static extern int git_strarray_copy(git_strarray *tgt, git_strarray *src);


		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Repository.h
		////////////////////////////////////////////////////////////////////////////////////////////////////////

		/**
		 * Open a git repository.
		 *
		 * The 'path' argument must point to either a git repository
		 * folder, or an existing work dir.
		 *
		 * The method will automatically detect if 'path' is a normal
		 * or bare repository or fail is 'path' is neither.
		 *
		 * @param out pointer to the repo which will be opened
		 * @param path the path to the repository
		 * @return 0 or an error code
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern int git_repository_open(out git_repository* outVal, char8* path);
		/**
		 * Open working tree as a repository
		 *
		 * Open the working directory of the working tree as a normal
		 * repository that can then be worked on.
		 *
		 * @param out Output pointer containing opened repository
		 * @param wt Working tree to open
		 * @return 0 or an error code
		 */
		public static extern int git_repository_open_from_worktree(git_repository** outVal, git_worktree *wt);
	
		/**
		 * Create a "fake" repository to wrap an object database
		 *
		 * Create a repository object to wrap an object database to be used
		 * with the API when all you have is an object database. This doesn't
		 * have any paths associated with it, so use with care.
		 *
		 * @param out pointer to the repo
		 * @param odb the object database to wrap
		 * @return 0 or an error code
		 */
		public static extern int git_repository_wrap_odb(git_repository** outVal, git_odb *odb);
	
		/**
		 * Look for a git repository and copy its path in the given buffer.
		 * The lookup start from base_path and walk across parent directories
		 * if nothing has been found. The lookup ends when the first repository
		 * is found, or when reaching a directory referenced in ceiling_dirs
		 * or when the filesystem changes (in case across_fs is true).
		 *
		 * The method will automatically detect if the repository is bare
		 * (if there is a repository).
		 *
		 * @param out A pointer to a user-allocated git_buf which will contain
		 * the found path.
		 *
		 * @param start_path The base path where the lookup starts.
		 *
		 * @param across_fs If true, then the lookup will not stop when a
		 * filesystem device change is detected while exploring parent directories.
		 *
		 * @param ceiling_dirs A GIT_PATH_LIST_SEPARATOR separated list of
		 * absolute symbolic link free paths. The lookup will stop when any
		 * of this paths is reached. Note that the lookup always performs on
		 * start_path no matter start_path appears in ceiling_dirs ceiling_dirs
		 * might be NULL (which is equivalent to an empty string)
		 *
		 * @return 0 or an error code
		 */
		public static extern int git_repository_discover(
				git_buf* outVal,
				char8* start_path,
				int across_fs,
				char8* ceiling_dirs);
	
		/**
		 * Option flags for `git_repository_open_ext`.
		 */
		public enum git_repository_open_flag_t : int32
		{
			/**
			 * Only open the repository if it can be immediately found in the
			 * start_path. Do not walk up from the start_path looking at parent
			 * directories.
			 */
			GIT_REPOSITORY_OPEN_NO_SEARCH = (1 << 0),
	
			/**
			 * Unless this flag is set, open will not continue searching across
			 * filesystem boundaries (i.e. when `st_dev` changes from the `stat`
			 * system call).  For example, searching in a user's home directory at
			 * "/home/user/source/" will not return "/.git/" as the found repo if
			 * "/" is a different filesystem than "/home".
			 */
			GIT_REPOSITORY_OPEN_CROSS_FS  = (1 << 1),
	
			/**
			 * Open repository as a bare repo regardless of core.bare config, and
			 * defer loading config file for faster setup.
			 * Unlike `git_repository_open_bare`, this can follow gitlinks.
			 */
			GIT_REPOSITORY_OPEN_BARE      = (1 << 2),
	
			/**
			 * Do not check for a repository by appending /.git to the start_path;
			 * only open the repository if start_path itself points to the git
			 * directory.
			 */
			GIT_REPOSITORY_OPEN_NO_DOTGIT = (1 << 3),
	
			/**
			 * Find and open a git repository, respecting the environment variables
			 * used by the git command-line tools.
			 * If set, `git_repository_open_ext` will ignore the other flags and
			 * the `ceiling_dirs` argument, and will allow a NULL `path` to use
			 * `GIT_DIR` or search from the current directory.
			 * The search for a repository will respect $GIT_CEILING_DIRECTORIES and
			 * $GIT_DISCOVERY_ACROSS_FILESYSTEM.  The opened repository will
			 * respect $GIT_INDEX_FILE, $GIT_NAMESPACE, $GIT_OBJECT_DIRECTORY, and
			 * $GIT_ALTERNATE_OBJECT_DIRECTORIES.
			 * In the future, this flag will also cause `git_repository_open_ext`
			 * to respect $GIT_WORK_TREE and $GIT_COMMON_DIR; currently,
			 * `git_repository_open_ext` with this flag will error out if either
			 * $GIT_WORK_TREE or $GIT_COMMON_DIR is set.
			 */
			GIT_REPOSITORY_OPEN_FROM_ENV  = (1 << 4),
		};
	
		/**
		 * Find and open a repository with extended controls.
		 *
		 * @param out Pointer to the repo which will be opened.  This can
		 *        actually be NULL if you only want to use the error code to
		 *        see if a repo at this path could be opened.
		 * @param path Path to open as git repository.  If the flags
		 *        permit "searching", then this can be a path to a subdirectory
		 *        inside the working directory of the repository. May be NULL if
		 *        flags is GIT_REPOSITORY_OPEN_FROM_ENV.
		 * @param flags A combination of the GIT_REPOSITORY_OPEN flags above.
		 * @param ceiling_dirs A GIT_PATH_LIST_SEPARATOR delimited list of path
		 *        prefixes at which the search for a containing repository should
		 *        terminate.
		 * @return 0 on success, GIT_ENOTFOUND if no repository could be found,
		 *        or -1 if there was a repository but open failed for some reason
		 *        (such as repo corruption or system errors).
		 */
		public static extern int git_repository_open_ext(
			git_repository** outVal,
			char8* path,
			uint32 flags,
			char8* ceiling_dirs);
	
		/**
		 * Open a bare repository on the serverside.
		 *
		 * This is a fast open for bare repositories that will come in handy
		 * if you're e.g. hosting git repositories and need to access them
		 * efficiently
		 *
		 * @param out Pointer to the repo which will be opened.
		 * @param bare_path Direct path to the bare repository
		 * @return 0 on success, or an error code
		 */
		public static extern int git_repository_open_bare(git_repository** outVal, char8* bare_path);
	
		/**
		 * Free a previously allocated repository
		 *
		 * Note that after a repository is free'd, all the objects it has spawned
		 * will still exist until they are manually closed by the user
		 * with `git_object_free`, but accessing any of the attributes of
		 * an object without a backing repository will result in undefined
		 * behavior
		 *
		 * @param repo repository handle to close. If NULL nothing occurs.
		 */
		public static extern void git_repository_free(git_repository *repo);
	
		/**
		 * Creates a new Git repository in the given folder.
		 *
		 * TODO:
		 *	- Reinit the repository
		 *
		 * @param out pointer to the repo which will be created or reinitialized
		 * @param path the path to the repository
		 * @param is_bare if true, a Git repository without a working directory is
		 *		created at the pointed path. If false, provided path will be
		 *		considered as the working directory into which the .git directory
		 *		will be created.
		 *
		 * @return 0 or an error code
		 */
		public static extern int git_repository_init(
			git_repository** outVal,
			char8* path,
			c_uint is_bare);
	
		/**
		 * Option flags for `git_repository_init_ext`.
		 *
		 * These flags configure extra behaviors to `git_repository_init_ext`.
		 * In every case, the default behavior is the zero value (i.e. flag is
		 * not set).  Just OR the flag values together for the `flags` parameter
		 * when initializing a new repo.  Details of individual values are:
		 *
		 * * BARE   - Create a bare repository with no working directory.
		 * * NO_REINIT - Return an GIT_EEXISTS error if the repo_path appears to
		 *        already be an git repository.
		 * * NO_DOTGIT_DIR - Normally a "/.git/" will be appended to the repo
		 *        path for non-bare repos (if it is not already there), but
		 *        passing this flag prevents that behavior.
		 * * MKDIR  - Make the repo_path (and workdir_path) as needed.  Init is
		 *        always willing to create the ".git" directory even without this
		 *        flag.  This flag tells init to create the trailing component of
		 *        the repo and workdir paths as needed.
		 * * MKPATH - Recursively make all components of the repo and workdir
		 *        paths as necessary.
		 * * EXTERNAL_TEMPLATE - libgit2 normally uses internal templates to
		 *        initialize a new repo.  This flags enables external templates,
		 *        looking the "template_path" from the options if set, or the
		 *        `init.templatedir` global config if not, or falling back on
		 *        "/usr/share/git-core/templates" if it exists.
		 * * GIT_REPOSITORY_INIT_RELATIVE_GITLINK - If an alternate workdir is
		 *        specified, use relative paths for the gitdir and core.worktree.
		 */
		enum git_repository_init_flag_t : int32
		{
			GIT_REPOSITORY_INIT_BARE              = (1u << 0),
			GIT_REPOSITORY_INIT_NO_REINIT         = (1u << 1),
			GIT_REPOSITORY_INIT_NO_DOTGIT_DIR     = (1u << 2),
			GIT_REPOSITORY_INIT_MKDIR             = (1u << 3),
			GIT_REPOSITORY_INIT_MKPATH            = (1u << 4),
			GIT_REPOSITORY_INIT_EXTERNAL_TEMPLATE = (1u << 5),
			GIT_REPOSITORY_INIT_RELATIVE_GITLINK  = (1u << 6),
		}
	
		/**
		 * Mode options for `git_repository_init_ext`.
		 *
		 * Set the mode field of the `git_repository_init_options` structure
		 * either to the custom mode that you would like, or to one of the
		 * following modes:
		 *
		 * * SHARED_UMASK - Use permissions configured by umask - the default.
		 * * SHARED_GROUP - Use "--shared=group" behavior, chmod'ing the new repo
		 *        to be group writable and "g+sx" for sticky group assignment.
		 * * SHARED_ALL - Use "--shared=all" behavior, adding world readability.
		 * * Anything else - Set to custom value.
		 */
		enum git_repository_init_mode_t : int32
		{
			GIT_REPOSITORY_INIT_SHARED_UMASK = 0,
			GIT_REPOSITORY_INIT_SHARED_GROUP = 0002775,
			GIT_REPOSITORY_INIT_SHARED_ALL   = 0002777,
		}
	
		/**
		 * Extended options structure for `git_repository_init_ext`.
		 *
		 * This contains extra options for `git_repository_init_ext` that enable
		 * additional initialization features.  The fields are:
		 *
		 * * flags - Combination of GIT_REPOSITORY_INIT flags above.
		 * * mode  - Set to one of the standard GIT_REPOSITORY_INIT_SHARED_...
		 *        constants above, or to a custom value that you would like.
		 * * workdir_path - The path to the working dir or NULL for default (i.e.
		 *        repo_path parent on non-bare repos).  IF THIS IS RELATIVE PATH,
		 *        IT WILL BE EVALUATED RELATIVE TO THE REPO_PATH.  If this is not
		 *        the "natural" working directory, a .git gitlink file will be
		 *        created here linking to the repo_path.
		 * * description - If set, this will be used to initialize the "description"
		 *        file in the repository, instead of using the template content.
		 * * template_path - When GIT_REPOSITORY_INIT_EXTERNAL_TEMPLATE is set,
		 *        this contains the path to use for the template directory.  If
		 *        this is NULL, the config or default directory options will be
		 *        used instead.
		 * * initial_head - The name of the head to point HEAD at.  If NULL, then
		 *        this will be treated as "master" and the HEAD ref will be set
		 *        to "refs/heads/master".  If this begins with "refs/" it will be
		 *        used verbatim; otherwise "refs/heads/" will be prefixed.
		 * * origin_url - If this is non-NULL, then after the rest of the
		 *        repository initialization is completed, an "origin" remote
		 *        will be added pointing to this URL.
		 */
		struct git_repository_init_options
		{
			c_uint version;
			uint32    flags;
			uint32    mode;
			char8* workdir_path;
			char8* description;
			char8* template_path;
			char8* initial_head;
			char8* origin_url;
		} ;
	
		//#define GIT_REPOSITORY_INIT_OPTIONS_VERSION 1
		//#define GIT_REPOSITORY_INIT_OPTIONS_INIT {GIT_REPOSITORY_INIT_OPTIONS_VERSION}
	
		/**
		 * Initialize git_repository_init_options structure
		 *
		 * Initializes a `git_repository_init_options` with default values. Equivalent to
		 * creating an instance with `GIT_REPOSITORY_INIT_OPTIONS_INIT`.
		 *
		 * @param opts The `git_repository_init_options` struct to initialize.
		 * @param version The struct version; pass `GIT_REPOSITORY_INIT_OPTIONS_VERSION`.
		 * @return Zero on success; -1 on failure.
		 */
		public static extern int git_repository_init_options_init(
			git_repository_init_options *opts,
			c_uint version);
	
		/**
		 * Create a new Git repository in the given folder with extended controls.
		 *
		 * This will initialize a new git repository (creating the repo_path
		 * if requested by flags) and working directory as needed.  It will
		 * auto-detect the case sensitivity of the file system and if the
		 * file system supports file mode bits correctly.
		 *
		 * @param out Pointer to the repo which will be created or reinitialized.
		 * @param repo_path The path to the repository.
		 * @param opts Pointer to git_repository_init_options struct.
		 * @return 0 or an error code on failure.
		 */
		public static extern int git_repository_init_ext(
			git_repository** outVal,
			char8* repo_path,
			git_repository_init_options *opts);
	
		/**
		 * Retrieve and resolve the reference pointed at by HEAD.
		 *
		 * The returned `git_reference` will be owned by caller and
		 * `git_reference_free()` must be called when done with it to release the
		 * allocated memory and prevent a leak.
		 *
		 * @param out pointer to the reference which will be retrieved
		 * @param repo a repository object
		 *
		 * @return 0 on success, GIT_EUNBORNBRANCH when HEAD points to a non existing
		 * branch, GIT_ENOTFOUND when HEAD is missing; an error code otherwise
		 */
		public static extern int git_repository_head(git_reference** outVal, git_repository *repo);
	
		/**
		 * Retrieve the referenced HEAD for the worktree
		 *
		 * @param out pointer to the reference which will be retrieved
		 * @param repo a repository object
		 * @param name name of the worktree to retrieve HEAD for
		 * @return 0 when successful, error-code otherwise
		 */
		public static extern int git_repository_head_for_worktree(git_reference** outVal, git_repository *repo,
			char8* name);
	
		/**
		 * Check if a repository's HEAD is detached
		 *
		 * A repository's HEAD is detached when it points directly to a commit
		 * instead of a branch.
		 *
		 * @param repo Repo to test
		 * @return 1 if HEAD is detached, 0 if it's not; error code if there
		 * was an error.
		 */
		public static extern int git_repository_head_detached(git_repository *repo);
	
		/**
		 * Check if a worktree's HEAD is detached
		 *
		 * A worktree's HEAD is detached when it points directly to a
		 * commit instead of a branch.
		 *
		 * @param repo a repository object
		 * @param name name of the worktree to retrieve HEAD for
		 * @return 1 if HEAD is detached, 0 if its not; error code if
		 *  there was an error
		 */
		public static extern int git_repository_head_detached_for_worktree(git_repository *repo,
			char8* name);
	
		/**
		 * Check if the current branch is unborn
		 *
		 * An unborn branch is one named from HEAD but which doesn't exist in
		 * the refs namespace, because it doesn't have any commit to point to.
		 *
		 * @param repo Repo to test
		 * @return 1 if the current branch is unborn, 0 if it's not; error
		 * code if there was an error
		 */
		public static extern int git_repository_head_unborn(git_repository *repo);
	
		/**
		 * Check if a repository is empty
		 *
		 * An empty repository has just been initialized and contains no references
		 * apart from HEAD, which must be pointing to the unborn master branch.
		 *
		 * @param repo Repo to test
		 * @return 1 if the repository is empty, 0 if it isn't, error code
		 * if the repository is corrupted
		 */
		public static extern int git_repository_is_empty(git_repository *repo);
	
		/**
		 * List of items which belong to the git repository layout
		 */
		public enum git_repository_item_t : int32
		{
			GIT_REPOSITORY_ITEM_GITDIR,
			GIT_REPOSITORY_ITEM_WORKDIR,
			GIT_REPOSITORY_ITEM_COMMONDIR,
			GIT_REPOSITORY_ITEM_INDEX,
			GIT_REPOSITORY_ITEM_OBJECTS,
			GIT_REPOSITORY_ITEM_REFS,
			GIT_REPOSITORY_ITEM_PACKED_REFS,
			GIT_REPOSITORY_ITEM_REMOTES,
			GIT_REPOSITORY_ITEM_CONFIG,
			GIT_REPOSITORY_ITEM_INFO,
			GIT_REPOSITORY_ITEM_HOOKS,
			GIT_REPOSITORY_ITEM_LOGS,
			GIT_REPOSITORY_ITEM_MODULES,
			GIT_REPOSITORY_ITEM_WORKTREES,
			GIT_REPOSITORY_ITEM__LAST
		}
	
		/**
		 * Get the location of a specific repository file or directory
		 *
		 * This function will retrieve the path of a specific repository
		 * item. It will thereby honor things like the repository's
		 * common directory, gitdir, etc. In case a file path cannot
		 * exist for a given item (e.g. the working directory of a bare
		 * repository), GIT_ENOTFOUND is returned.
		 *
		 * @param out Buffer to store the path at
		 * @param repo Repository to get path for
		 * @param item The repository item for which to retrieve the path
		 * @return 0, GIT_ENOTFOUND if the path cannot exist or an error code
		 */
		public static extern int git_repository_item_path(git_buf *outVal, git_repository *repo, git_repository_item_t item);
	
		/**
		 * Get the path of this repository
		 *
		 * This is the path of the `.git` folder for normal repositories,
		 * or of the repository itself for bare repositories.
		 *
		 * @param repo A repository object
		 * @return the path to the repository
		 */
		public static extern char8*  git_repository_path(git_repository *repo);
	
		/**
		 * Get the path of the working directory for this repository
		 *
		 * If the repository is bare, this function will always return
		 * NULL.
		 *
		 * @param repo A repository object
		 * @return the path to the working dir, if it exists
		 */
		public static extern char8*  git_repository_workdir(git_repository* repo);
	
		/**
		 * Get the path of the shared common directory for this repository.
		 * 
		 * If the repository is bare, it is the root directory for the repository.
		 * If the repository is a worktree, it is the parent repo's gitdir.
		 * Otherwise, it is the gitdir.
		 *
		 * @param repo A repository object
		 * @return the path to the common dir
		 */
		public static extern char8*  git_repository_commondir(git_repository* repo);
	
		/**
		 * Set the path to the working directory for this repository
		 *
		 * The working directory doesn't need to be the same one
		 * that contains the `.git` folder for this repository.
		 *
		 * If this repository is bare, setting its working directory
		 * will turn it into a normal repository, capable of performing
		 * all the common workdir operations (checkout, status, index
		 * manipulation, etc).
		 *
		 * @param repo A repository object
		 * @param workdir The path to a working directory
		 * @param update_gitlink Create/update gitlink in workdir and set config
		 *        "core.worktree" (if workdir is not the parent of the .git directory)
		 * @return 0, or an error code
		 */
		public static extern int git_repository_set_workdir(
			git_repository *repo, char8* workdir, int update_gitlink);
	
		/**
		 * Check if a repository is bare
		 *
		 * @param repo Repo to test
		 * @return 1 if the repository is bare, 0 otherwise.
		 */
		public static extern int git_repository_is_bare(git_repository* repo);
	
		/**
		 * Check if a repository is a linked work tree
		 *
		 * @param repo Repo to test
		 * @return 1 if the repository is a linked work tree, 0 otherwise.
		 */
		public static extern int git_repository_is_worktree(git_repository* repo);
	
		/**
		 * Get the configuration file for this repository.
		 *
		 * If a configuration file has not been set, the default
		 * config set for the repository will be returned, including
		 * global and system configurations (if they are available).
		 *
		 * The configuration file must be freed once it's no longer
		 * being used by the user.
		 *
		 * @param out Pointer to store the loaded configuration
		 * @param repo A repository object
		 * @return 0, or an error code
		 */
		public static extern int git_repository_config(git_config** outVal, git_repository *repo);
	
		/**
		 * Get a snapshot of the repository's configuration
		 *
		 * Convenience function to take a snapshot from the repository's
		 * configuration.  The contents of this snapshot will not change,
		 * even if the underlying config files are modified.
		 *
		 * The configuration file must be freed once it's no longer
		 * being used by the user.
		 *
		 * @param out Pointer to store the loaded configuration
		 * @param repo the repository
		 * @return 0, or an error code
		 */
		public static extern int git_repository_config_snapshot(git_config** outVal, git_repository *repo);
	
		/**
		 * Get the Object Database for this repository.
		 *
		 * If a custom ODB has not been set, the default
		 * database for the repository will be returned (the one
		 * located in `.git/objects`).
		 *
		 * The ODB must be freed once it's no longer being used by
		 * the user.
		 *
		 * @param out Pointer to store the loaded ODB
		 * @param repo A repository object
		 * @return 0, or an error code
		 */
		public static extern int git_repository_odb(git_odb** outVal, git_repository *repo);
	
		/**
		 * Get the Reference Database Backend for this repository.
		 *
		 * If a custom refsdb has not been set, the default database for
		 * the repository will be returned (the one that manipulates loose
		 * and packed references in the `.git` directory).
		 *
		 * The refdb must be freed once it's no longer being used by
		 * the user.
		 *
		 * @param out Pointer to store the loaded refdb
		 * @param repo A repository object
		 * @return 0, or an error code
		 */
		public static extern int git_repository_refdb(git_refdb** outVal, git_repository *repo);
	
		/**
		 * Get the Index file for this repository.
		 *
		 * If a custom index has not been set, the default
		 * index for the repository will be returned (the one
		 * located in `.git/index`).
		 *
		 * The index must be freed once it's no longer being used by
		 * the user.
		 *
		 * @param out Pointer to store the loaded index
		 * @param repo A repository object
		 * @return 0, or an error code
		 */
		public static extern int git_repository_index(git_index** outVal, git_repository *repo);
	
		/**
		 * Retrieve git's prepared message
		 *
		 * Operations such as git revert/cherry-pick/merge with the -n option
		 * stop just short of creating a commit with the changes and save
		 * their prepared message in .git/MERGE_MSG so the next git-commit
		 * execution can present it to the user for them to amend if they
		 * wish.
		 *
		 * Use this function to get the contents of this file. Don't forget to
		 * remove the file after you create the commit.
		 *
		 * @param out git_buf to write data into
		 * @param repo Repository to read prepared message from
		 * @return 0, GIT_ENOTFOUND if no message exists or an error code
		 */
		public static extern int git_repository_message(git_buf* outVal, git_repository *repo);
	
		/**
		 * Remove git's prepared message.
		 *
		 * Remove the message that `git_repository_message` retrieves.
		 */
		public static extern int git_repository_message_remove(git_repository *repo);
	
		/**
		 * Remove all the metadata associated with an ongoing command like merge,
		 * revert, cherry-pick, etc.  For example: MERGE_HEAD, MERGE_MSG, etc.
		 *
		 * @param repo A repository object
		 * @return 0 on success, or error
		 */
		public static extern int git_repository_state_cleanup(git_repository *repo);
	
		/**
		 * Callback used to iterate over each FETCH_HEAD entry
		 *
		 * @see git_repository_fetchhead_foreach
		 *
		 * @param ref_name The reference name
		 * @param remote_url The remote URL
		 * @param oid The reference target OID
		 * @param is_merge Was the reference the result of a merge
		 * @param payload Payload passed to git_repository_fetchhead_foreach
		 * @return non-zero to terminate the iteration
		 */
		public function c_int git_repository_fetchhead_foreach_cb(char8* ref_name,
			char8* remote_url,
			git_oid *oid,
			c_uint is_merge,
			void *payload);
	
		/**
		 * Invoke 'callback' for each entry in the given FETCH_HEAD file.
		 *
		 * Return a non-zero value from the callback to stop the loop.
		 *
		 * @param repo A repository object
		 * @param callback Callback function
		 * @param payload Pointer to callback data (optional)
		 * @return 0 on success, non-zero callback return value, GIT_ENOTFOUND if
		 *         there is no FETCH_HEAD file, or other error code.
		 */
		public static extern int git_repository_fetchhead_foreach(
			git_repository *repo,
			git_repository_fetchhead_foreach_cb callback,
			void *payload);
	
		/**
		 * Callback used to iterate over each MERGE_HEAD entry
		 *
		 * @see git_repository_mergehead_foreach
		 *
		 * @param oid The merge OID
		 * @param payload Payload passed to git_repository_mergehead_foreach
		 * @return non-zero to terminate the iteration
		 */
		public function int git_repository_mergehead_foreach_cb(git_oid *oid, void *payload);
	
		/**
		 * If a merge is in progress, invoke 'callback' for each commit ID in the
		 * MERGE_HEAD file.
		 *
		 * Return a non-zero value from the callback to stop the loop.
		 *
		 * @param repo A repository object
		 * @param callback Callback function
		 * @param payload Pointer to callback data (optional)
		 * @return 0 on success, non-zero callback return value, GIT_ENOTFOUND if
		 *         there is no MERGE_HEAD file, or other error code.
		 */
		public static extern int git_repository_mergehead_foreach(
			git_repository *repo,
			git_repository_mergehead_foreach_cb callback,
			void *payload);
	
		/**
		 * Calculate hash of file using repository filtering rules.
		 *
		 * If you simply want to calculate the hash of a file on disk with no filters,
		 * you can just use the `git_odb_hashfile()` API.  However, if you want to
		 * hash a file in the repository and you want to apply filtering rules (e.g.
		 * crlf filters) before generating the SHA, then use this function.
		 *
		 * Note: if the repository has `core.safecrlf` set to fail and the
		 * filtering triggers that failure, then this function will return an
		 * error and not calculate the hash of the file.
		 *
		 * @param out Output value of calculated SHA
		 * @param repo Repository pointer
		 * @param path Path to file on disk whose contents should be hashed. If the
		 *             repository is not NULL, this can be a relative path.
		 * @param type The object type to hash as (e.g. GIT_OBJECT_BLOB)
		 * @param as_path The path to use to look up filtering rules. If this is
		 *             NULL, then the `path` parameter will be used instead. If
		 *             this is passed as the empty string, then no filters will be
		 *             applied when calculating the hash.
		 * @return 0 on success, or an error code
		 */
		public static extern int git_repository_hashfile(
			git_oid *outVal,
			git_repository *repo,
			char8* path,
			git_object_t type,
			char8* as_path);
	
		/**
		 * Make the repository HEAD point to the specified reference.
		 *
		 * If the provided reference points to a Tree or a Blob, the HEAD is
		 * unaltered and -1 is returned.
		 *
		 * If the provided reference points to a branch, the HEAD will point
		 * to that branch, staying attached, or become attached if it isn't yet.
		 * If the branch doesn't exist yet, no error will be return. The HEAD
		 * will then be attached to an unborn branch.
		 *
		 * Otherwise, the HEAD will be detached and will directly point to
		 * the Commit.
		 *
		 * @param repo Repository pointer
		 * @param refname Canonical name of the reference the HEAD should point at
		 * @return 0 on success, or an error code
		 */
		public static extern int git_repository_set_head(
			git_repository* repo,
			char8* refname);
	
		/**
		 * Make the repository HEAD directly point to the Commit.
		 *
		 * If the provided committish cannot be found in the repository, the HEAD
		 * is unaltered and GIT_ENOTFOUND is returned.
		 *
		 * If the provided commitish cannot be peeled into a commit, the HEAD
		 * is unaltered and -1 is returned.
		 *
		 * Otherwise, the HEAD will eventually be detached and will directly point to
		 * the peeled Commit.
		 *
		 * @param repo Repository pointer
		 * @param commitish Object id of the Commit the HEAD should point to
		 * @return 0 on success, or an error code
		 */
		public static extern int git_repository_set_head_detached(
			git_repository* repo,
			git_oid* commitish);
	
		/**
		 * Make the repository HEAD directly point to the Commit.
		 *
		 * This behaves like `git_repository_set_head_detached()` but takes an
		 * annotated commit, which lets you specify which extended sha syntax
		 * string was specified by a user, allowing for more exact reflog
		 * messages.
		 *
		 * See the documentation for `git_repository_set_head_detached()`.
		 *
		 * @see git_repository_set_head_detached
		 */
		public static extern int git_repository_set_head_detached_from_annotated(
			git_repository *repo,
			git_annotated_commit *commitish);
	
		/**
		 * Detach the HEAD.
		 *
		 * If the HEAD is already detached and points to a Commit, 0 is returned.
		 *
		 * If the HEAD is already detached and points to a Tag, the HEAD is
		 * updated into making it point to the peeled Commit, and 0 is returned.
		 *
		 * If the HEAD is already detached and points to a non commitish, the HEAD is
		 * unaltered, and -1 is returned.
		 *
		 * Otherwise, the HEAD will be detached and point to the peeled Commit.
		 *
		 * @param repo Repository pointer
		 * @return 0 on success, GIT_EUNBORNBRANCH when HEAD points to a non existing
		 * branch or an error code
		 */
		public static extern int git_repository_detach_head(
			git_repository* repo);
	
		/**
		 * Repository state
		 *
		 * These values represent possible states for the repository to be in,
		 * based on the current operation which is ongoing.
		 */
		public enum git_repository_state_t : c_int
		{
			GIT_REPOSITORY_STATE_NONE,
			GIT_REPOSITORY_STATE_MERGE,
			GIT_REPOSITORY_STATE_REVERT,
			GIT_REPOSITORY_STATE_REVERT_SEQUENCE,
			GIT_REPOSITORY_STATE_CHERRYPICK,
			GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE,
			GIT_REPOSITORY_STATE_BISECT,
			GIT_REPOSITORY_STATE_REBASE,
			GIT_REPOSITORY_STATE_REBASE_INTERACTIVE,
			GIT_REPOSITORY_STATE_REBASE_MERGE,
			GIT_REPOSITORY_STATE_APPLY_MAILBOX,
			GIT_REPOSITORY_STATE_APPLY_MAILBOX_OR_REBASE,
		}
	
		/**
		 * Determines the status of a git repository - ie, whether an operation
		 * (merge, cherry-pick, etc) is in progress.
		 *
		 * @param repo Repository pointer
		 * @return The state of the repository
		 */
		public static extern int git_repository_state(git_repository *repo);
	
		/**
		 * Sets the active namespace for this Git Repository
		 *
		 * This namespace affects all reference operations for the repo.
		 * See `man gitnamespaces`
		 *
		 * @param repo The repo
		 * @param nmspace The namespace. This should not include the refs
		 *	folder, e.g. to namespace all references under `refs/namespaces/foo/`,
		 *	use `foo` as the namespace.
		 *	@return 0 on success, -1 on error
		 */
		public static extern int git_repository_set_namespace(git_repository *repo, char8* nmspace);
	
		/**
		 * Get the currently active namespace for this repository
		 *
		 * @param repo The repo
		 * @return the active namespace, or NULL if there isn't one
		 */
		public static extern char8*  git_repository_get_namespace(git_repository *repo);
	
	
		/**
		 * Determine if the repository was a shallow clone
		 *
		 * @param repo The repository
		 * @return 1 if shallow, zero if not
		 */
		public static extern int git_repository_is_shallow(git_repository *repo);
	
		/**
		 * Retrieve the configured identity to use for reflogs
		 *
		 * The memory is owned by the repository and must not be freed by the
		 * user.
		 *
		 * @param name where to store the pointer to the name
		 * @param email where to store the pointer to the email
		 * @param repo the repository
		 */
		public static extern int git_repository_ident(char8* *name, char8* *email, git_repository* repo);
	
		/**
		 * Set the identity to be used for writing reflogs
		 *
		 * If both are set, this name and email will be used to write to the
		 * reflog. Pass NULL to unset. When unset, the identity will be taken
		 * from the repository's configuration.
		 *
		 * @param repo the repository to configure
		 * @param name the name to use for the reflog entries
		 * @param email the email to use for the reflog entries
		 */
		public static extern int git_repository_set_ident(git_repository *repo, char8* name, char8* email);
	
		/** @} */

		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// remote.h
		////////////////////////////////////////////////////////////////////////////////////////////////////////

		
		/**
		 * Add a remote with the default fetch refspec to the repository's configuration.
		 *
		 * @param out the resulting remote
		 * @param repo the repository in which to create the remote
		 * @param name the remote's name
		 * @param url the remote's url
		 * @return 0, GIT_EINVALIDSPEC, GIT_EEXISTS or an error code
		 */
		public static extern int git_remote_create(
				out git_remote *outVal,
				git_repository *repo,
				char8* name,
				char8* url);

		/**
		 * Remote creation options flags
		 */
		enum git_remote_create_flags : c_int
		{
			/** Ignore the repository apply.insteadOf configuration */
			GIT_REMOTE_CREATE_SKIP_INSTEADOF = (1 << 0),

			/** Don't build a fetchspec from the name if none is set */
			GIT_REMOTE_CREATE_SKIP_DEFAULT_FETCHSPEC = (1 << 1),
		}

		/**
		 * Remote creation options structure
		 *
		 * Initialize with `GIT_REMOTE_CREATE_OPTIONS_INIT`. Alternatively, you can
		 * use `git_remote_create_options_init`.
		 *
		 */
		public struct git_remote_create_options {
			public c_uint version;

			/**
			 * The repository that should own the remote.
			 * Setting this to NULL results in a detached remote.
			 */
			public git_repository *repository;

			/**
			 * The remote's name.
			 * Setting this to NULL results in an in-memory/anonymous remote.
			 */
			public char8* name;

			/** The fetchspec the remote should use. */
			public char8* fetchspec;

			/** Additional flags for the remote. See git_remote_create_flags. */
			public c_uint flags;
		}

		//#define GIT_REMOTE_CREATE_OPTIONS_VERSION 1
		//#define GIT_REMOTE_CREATE_OPTIONS_INIT {GIT_REMOTE_CREATE_OPTIONS_VERSION}

		/**
		 * Initialize git_remote_create_options structure
		 *
		 * Initializes a `git_remote_create_options` with default values. Equivalent to
		 * creating an instance with `GIT_REMOTE_CREATE_OPTIONS_INIT`.
		 *
		 * @param opts The `git_remote_create_options` struct to initialize.
		 * @param version The struct version; pass `GIT_REMOTE_CREATE_OPTIONS_VERSION`.
		 * @return Zero on success; -1 on failure.
		 */
		public static extern int git_remote_create_options_init(
				git_remote_create_options *opts,
				c_uint version);

		/**
		 * Create a remote, with options.
		 *
		 * This function allows more fine-grained control over the remote creation.
		 *
		 * Passing NULL as the opts argument will result in a detached remote.
		 *
		 * @param out the resulting remote
		 * @param url the remote's url
		 * @param opts the remote creation options
		 * @return 0, GIT_EINVALIDSPEC, GIT_EEXISTS or an error code
		 */
		public static extern int git_remote_create_with_opts(
				git_remote **outVal,
				char8* url,
				git_remote_create_options *opts);

		/**
		 * Add a remote with the provided fetch refspec (or default if NULL) to the repository's
		 * configuration.
		 *
		 * @param out the resulting remote
		 * @param repo the repository in which to create the remote
		 * @param name the remote's name
		 * @param url the remote's url
		 * @param fetch the remote fetch value
		 * @return 0, GIT_EINVALIDSPEC, GIT_EEXISTS or an error code
		 */
		public static extern int git_remote_create_with_fetchspec(
				git_remote **outVal,
				git_repository *repo,
				char8* name,
				char8* url,
				char8* fetch);

		/**
		 * Create an anonymous remote
		 *
		 * Create a remote with the given url in-memory. You can use this when
		 * you have a URL instead of a remote's name.
		 *
		 * @param out pointer to the new remote objects
		 * @param repo the associated repository
		 * @param url the remote repository's URL
		 * @return 0 or an error code
		 */
		public static extern int git_remote_create_anonymous(
				git_remote **outVal,
				git_repository *repo,
				char8* url);

		/**
		 * Create a remote without a connected local repo
		 *
		 * Create a remote with the given url in-memory. You can use this when
		 * you have a URL instead of a remote's name.
		 *
		 * Contrasted with git_remote_create_anonymous, a detached remote
		 * will not consider any repo configuration values (such as insteadof url
		 * substitutions).
		 *
		 * @param out pointer to the new remote objects
		 * @param url the remote repository's URL
		 * @return 0 or an error code
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern c_int git_remote_create_detached(
				out git_remote *outVal,
				char8* url);

		/**
		 * Get the information for a particular remote
		 *
		 * The name will be checked for validity.
		 * See `git_tag_create()` for rules about valid names.
		 *
		 * @param out pointer to the new remote object
		 * @param repo the associated repository
		 * @param name the remote's name
		 * @return 0, GIT_ENOTFOUND, GIT_EINVALIDSPEC or an error code
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern int git_remote_lookup(git_remote **outVal, git_repository *repo, char8* name);

		/**
		 * Create a copy of an existing remote.  All internal strings are also
		 * duplicated. Callbacks are not duplicated.
		 *
		 * Call `git_remote_free` to free the data.
		 *
		 * @param dest pointer where to store the copy
		 * @param source object to copy
		 * @return 0 or an error code
		 */
		public static extern int git_remote_dup(git_remote **dest, git_remote *source);

		/**
		 * Get the remote's repository
		 *
		 * @param remote the remote
		 * @return a pointer to the repository
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern git_repository * git_remote_owner(git_remote *remote);

		/**
		 * Get the remote's name
		 *
		 * @param remote the remote
		 * @return a pointer to the name or NULL for in-memory remotes
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern char8*  git_remote_name(git_remote *remote);

		/**
		 * Get the remote's url
		 *
		 * If url.*.insteadOf has been configured for this URL, it will
		 * return the modified URL.
		 *
		 * @param remote the remote
		 * @return a pointer to the url
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern char8*  git_remote_url(git_remote *remote);

		/**
		 * Get the remote's url for pushing
		 *
		 * If url.*.pushInsteadOf has been configured for this URL, it
		 * will return the modified URL.
		 *
		 * @param remote the remote
		 * @return a pointer to the url or NULL if no special url for pushing is set
		 */
		public static extern char8*  git_remote_pushurl(git_remote *remote);

		/**
		 * Set the remote's url in the configuration
		 *
		 * Remote objects already in memory will not be affected. This assumes
		 * the common case of a single-url remote and will otherwise return an error.
		 *
		 * @param repo the repository in which to perform the change
		 * @param remote the remote's name
		 * @param url the url to set
		 * @return 0 or an error value
		 */
		public static extern int git_remote_set_url(git_repository *repo, char8* remote, char8* url);

		/**
		 * Set the remote's url for pushing in the configuration.
		 *
		 * Remote objects already in memory will not be affected. This assumes
		 * the common case of a single-url remote and will otherwise return an error.
		 *
		 *
		 * @param repo the repository in which to perform the change
		 * @param remote the remote's name
		 * @param url the url to set
		 */
		public static extern int git_remote_set_pushurl(git_repository *repo, char8* remote, char8* url);

		/**
		 * Add a fetch refspec to the remote's configuration
		 *
		 * Add the given refspec to the fetch list in the configuration. No
		 * loaded remote instances will be affected.
		 *
		 * @param repo the repository in which to change the configuration
		 * @param remote the name of the remote to change
		 * @param refspec the new fetch refspec
		 * @return 0, GIT_EINVALIDSPEC if refspec is invalid or an error value
		 */
		public static extern int git_remote_add_fetch(git_repository *repo, char8* remote, char8* refspec);

		/**
		 * Get the remote's list of fetch refspecs
		 *
		 * The memory is owned by the user and should be freed with
		 * `git_strarray_free`.
		 *
		 * @param array pointer to the array in which to store the strings
		 * @param remote the remote to query
		 */
		public static extern int git_remote_get_fetch_refspecs(git_strarray *array, git_remote *remote);

		/**
		 * Add a push refspec to the remote's configuration
		 *
		 * Add the given refspec to the push list in the configuration. No
		 * loaded remote instances will be affected.
		 *
		 * @param repo the repository in which to change the configuration
		 * @param remote the name of the remote to change
		 * @param refspec the new push refspec
		 * @return 0, GIT_EINVALIDSPEC if refspec is invalid or an error value
		 */
		public static extern int git_remote_add_push(git_repository *repo, char8* remote, char8* refspec);

		/**
		 * Get the remote's list of push refspecs
		 *
		 * The memory is owned by the user and should be freed with
		 * `git_strarray_free`.
		 *
		 * @param array pointer to the array in which to store the strings
		 * @param remote the remote to query
		 */
		public static extern int git_remote_get_push_refspecs(git_strarray *array, git_remote *remote);

		/**
		 * Get the number of refspecs for a remote
		 *
		 * @param remote the remote
		 * @return the amount of refspecs configured in this remote
		 */
		public static extern c_size git_remote_refspec_count(git_remote *remote);

		/**
		 * Get a refspec from the remote
		 *
		 * @param remote the remote to query
		 * @param n the refspec to get
		 * @return the nth refspec
		 */
		public static extern git_refspec* git_remote_get_refspec(git_remote *remote, c_size n);

		/**
		 * Open a connection to a remote
		 *
		 * The transport is selected based on the URL. The direction argument
		 * is due to a limitation of the git protocol (over TCP or SSH) which
		 * starts up a specific binary which can only do the one or the other.
		 *
		 * @param remote the remote to connect to
		 * @param direction GIT_DIRECTION_FETCH if you want to fetch or
		 * GIT_DIRECTION_PUSH if you want to push
		 * @param callbacks the callbacks to use for this connection
		 * @param proxy_opts proxy settings
		 * @param custom_headers extra HTTP headers to use in this connection
		 * @return 0 or an error code
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern c_int git_remote_connect(git_remote *remote, git_direction direction, git_remote_callbacks *callbacks, git_proxy_options *proxy_opts, git_strarray *custom_headers);

		/**
		 * Get the remote repository's reference advertisement list
		 *
		 * Get the list of references with which the server responds to a new
		 * connection.
		 *
		 * The remote (or more exactly its transport) must have connected to
		 * the remote repository. This list is available as soon as the
		 * connection to the remote is initiated and it remains available
		 * after disconnecting.
		 *
		 * The memory belongs to the remote. The pointer will be valid as long
		 * as a new connection is not initiated, but it is recommended that
		 * you make a copy in order to make use of the data.
		 *
		 * @param out pointer to the array
		 * @param size the number of remote heads
		 * @param remote the remote
		 * @return 0 on success, or an error code
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern c_int git_remote_ls(out git_remote_head **outVal, out c_size size, git_remote* remote);

		/**
		 * Check whether the remote is connected
		 *
		 * Check whether the remote's underlying transport is connected to the
		 * remote host.
		 *
		 * @param remote the remote
		 * @return 1 if it's connected, 0 otherwise.
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern c_int git_remote_connected(git_remote *remote);

		/**
		 * Cancel the operation
		 *
		 * At certain points in its operation, the network code checks whether
		 * the operation has been cancelled and if so stops the operation.
		 *
		 * @param remote the remote
		 * @return 0 on success, or an error code
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern c_int git_remote_stop(git_remote *remote);

		/**
		 * Disconnect from the remote
		 *
		 * Close the connection to the remote.
		 *
		 * @param remote the remote to disconnect from
		 * @return 0 on success, or an error code
		 */
		public static extern int git_remote_disconnect(git_remote *remote);

		/**
		 * Free the memory associated with a remote
		 *
		 * This also disconnects from the remote, if the connection
		 * has not been closed yet (using git_remote_disconnect).
		 *
		 * @param remote the remote to free
		 */
		public static extern void git_remote_free(git_remote *remote);

		/**
		 * Get a list of the configured remotes for a repo
		 *
		 * The string array must be freed by the user.
		 *
		 * @param out a string array which receives the names of the remotes
		 * @param repo the repository to query
		 * @return 0 or an error code
		 */
		public static extern int git_remote_list(git_strarray* outVal, git_repository *repo);

		/**
		 * Argument to the completion callback which tells it which operation
		 * finished.
		 */
		public enum git_remote_completion_t : c_int {
			GIT_REMOTE_COMPLETION_DOWNLOAD,
			GIT_REMOTE_COMPLETION_INDEXING,
			GIT_REMOTE_COMPLETION_ERROR,
		}

		/** Push network progress notification function */
		public function c_int git_push_transfer_progress_cb(
			c_uint current,
			c_uint total,
			c_size bytes,
			void* payload);

		/**
		 * Represents an update which will be performed on the remote during push
		 */
		public struct git_push_update
		{
			/**
			 * The source name of the reference
			 */
			public char8 *src_refname;
			/**
			 * The name of the reference to update on the server
			 */
			public char8 *dst_refname;
			/**
			 * The current target of the reference
			 */
			public git_oid src;
			/**
			 * The new target for the reference
			 */
			public git_oid dst;
		}

		/**
		 * Callback used to inform of upcoming updates.
		 *
		 * @param updates an array containing the updates which will be sent
		 * as commands to the destination.
		 * @param len number of elements in `updates`
		 * @param payload Payload provided by the caller
		 */
		public function c_int git_push_negotiation(git_push_update **updates, c_size len, void *payload);

		/**
		 * Callback used to inform of the update status from the remote.
		 *
		 * Called for each updated reference on push. If `status` is
		 * not `NULL`, the update was rejected by the remote server
		 * and `status` contains the reason given.
		 *
		 * @param refname refname specifying to the remote ref
		 * @param status status message sent from the remote
		 * @param data data provided by the caller
		 * @return 0 on success, otherwise an error
		 */
		public function c_int git_push_update_reference_cb(char8* refname, char8* status, void *data);

		/**
		 * Callback to resolve URLs before connecting to remote
		 *
		 * If you return GIT_PASSTHROUGH, you don't need to write anything to
		 * url_resolved.
		 *
		 * @param url_resolved The buffer to write the resolved URL to
		 * @param url The URL to resolve
		 * @param direction GIT_DIRECTION_FETCH or GIT_DIRECTION_PUSH
		 * @param payload Payload provided by the caller
		 * @return 0 on success, GIT_PASSTHROUGH or an error
		 */
		public function c_int git_url_resolve_cb(git_buf *url_resolved, char8* url, int direction, void *payload);

		/**
		 * The callback settings structure
		 *
		 * Set the callbacks to be called by the remote when informing the user
		 * about the progress of the network operations.
		 */
		struct git_remote_callbacks
		{
			public c_uint version; /**< The version */

			/**
			 * Textual progress from the remote. Text send over the
			 * progress side-band will be passed to this function (this is
			 * the 'counting objects' output).
			 */
			public git_transport_message_cb sideband_progress;

			/**
			 * Completion is called when different parts of the download
			 * process are done (currently unused).
			 */
			public function int (git_remote_completion_t type, void *data) completion;

			/**
			 * This will be called if the remote host requires
			 * authentication in order to connect to it.
			 *
			 * Returning GIT_PASSTHROUGH will make libgit2 behave as
			 * though this field isn't set.
			 */
			public git_credential_acquire_cb credentials;

			/**
			 * If cert verification fails, this will be called to let the
			 * user make the final decision of whether to allow the
			 * connection to proceed. Returns 0 to allow the connection
			 * or a negative value to indicate an error.
			 */
			public git_transport_certificate_check_cb certificate_check;

			/**
			 * During the download of new data, this will be regularly
			 * called with the current count of progress done by the
			 * indexer.
			 */
			public git_indexer_progress_cb transfer_progress;

			/**
			 * Each time a reference is updated locally, this function
			 * will be called with information about it.
			 */
			public function int (char8* refname, git_oid *a, git_oid *b, void *data) update_tips;

			/**
			 * Function to call with progress information during pack
			 * building. Be aware that this is called inline with pack
			 * building operations, so performance may be affected.
			 */
			public git_packbuilder_progress pack_progress;

			/**
			 * Function to call with progress information during the
			 * upload portion of a push. Be aware that this is called
			 * inline with pack building operations, so performance may be
			 * affected.
			 */
			public git_push_transfer_progress_cb push_transfer_progress;

			/**
			 * See documentation of git_push_update_reference_cb
			 */
			public git_push_update_reference_cb push_update_reference;

			/**
			 * Called once between the negotiation step and the upload. It
			 * provides information about what updates will be performed.
			 */
			public git_push_negotiation push_negotiation;

			/**
			 * Create the transport to use for this operation. Leave NULL
			 * to auto-detect.
			 */
			public git_transport_cb transport;

			/**
			 * This will be passed to each of the callbacks in this struct
			 * as the last parameter.
			 */
			public void *payload;

			/**
			 * Resolve URL before connecting to remote.
			 * The returned URL will be used to connect to the remote instead.
			 */
			public git_url_resolve_cb resolve_url;
		};

		//#define GIT_REMOTE_CALLBACKS_VERSION 1
		//#define GIT_REMOTE_CALLBACKS_INIT {GIT_REMOTE_CALLBACKS_VERSION}

		/**
		 * Initializes a `git_remote_callbacks` with default values. Equivalent to
		 * creating an instance with GIT_REMOTE_CALLBACKS_INIT.
		 *
		 * @param opts the `git_remote_callbacks` struct to initialize
		 * @param version Version of struct; pass `GIT_REMOTE_CALLBACKS_VERSION`
		 * @return Zero on success; -1 on failure.
		 */
		public static extern int git_remote_init_callbacks(
			git_remote_callbacks *opts,
			c_uint version);

		/** Acceptable prune settings when fetching */
		public enum git_fetch_prune_t : c_int {
			/**
			 * Use the setting from the configuration
			 */
			GIT_FETCH_PRUNE_UNSPECIFIED,
			/**
			 * Force pruning on
			 */
			GIT_FETCH_PRUNE,
			/**
			 * Force pruning off
			 */
			GIT_FETCH_NO_PRUNE,
		}

		/**
		 * Automatic tag following option
		 *
		 * Lets us select the --tags option to use.
		 */
		public enum git_remote_autotag_option_t : c_int
		{
			/**
			 * Use the setting from the configuration.
			 */
			GIT_REMOTE_DOWNLOAD_TAGS_UNSPECIFIED = 0,
			/**
			 * Ask the server for tags pointing to objects we're already
			 * downloading.
			 */
			GIT_REMOTE_DOWNLOAD_TAGS_AUTO,
			/**
			 * Don't ask for any tags beyond the refspecs.
			 */
			GIT_REMOTE_DOWNLOAD_TAGS_NONE,
			/**
			 * Ask for the all the tags.
			 */
			GIT_REMOTE_DOWNLOAD_TAGS_ALL,
		}

		/**
		 * Fetch options structure.
		 *
		 * Zero out for defaults.  Initialize with `GIT_FETCH_OPTIONS_INIT` macro to
		 * correctly set the `version` field.  E.g.
		 *
		 *		git_fetch_options opts = GIT_FETCH_OPTIONS_INIT;
		 */
		public struct git_fetch_options
		{
			c_int version;

			/**
			 * Callbacks to use for this fetch operation
			 */
			git_remote_callbacks callbacks;

			/**
			 * Whether to perform a prune after the fetch
			 */
			git_fetch_prune_t prune;

			/**
			 * Whether to write the results to FETCH_HEAD. Defaults to
			 * on. Leave this default in order to behave like git.
			 */
			int update_fetchhead;

			/**
			 * Determines how to behave regarding tags on the remote, such
			 * as auto-downloading tags for objects we're downloading or
			 * downloading all of them.
			 *
			 * The default is to auto-follow tags.
			 */
			git_remote_autotag_option_t download_tags;

			/**
			 * Proxy options to use, by default no proxy is used.
			 */
			git_proxy_options proxy_opts;

			/**
			 * Extra headers for this fetch operation
			 */
			git_strarray custom_headers;
		}

		//#define GIT_FETCH_OPTIONS_VERSION 1
		//#define GIT_FETCH_OPTIONS_INIT { GIT_FETCH_OPTIONS_VERSION, GIT_REMOTE_CALLBACKS_INIT, GIT_FETCH_PRUNE_UNSPECIFIED, 1, \
		//				 GIT_REMOTE_DOWNLOAD_TAGS_UNSPECIFIED, GIT_PROXY_OPTIONS_INIT }

		/**
		 * Initialize git_fetch_options structure
		 *
		 * Initializes a `git_fetch_options` with default values. Equivalent to
		 * creating an instance with `GIT_FETCH_OPTIONS_INIT`.
		 *
		 * @param opts The `git_fetch_options` struct to initialize.
		 * @param version The struct version; pass `GIT_FETCH_OPTIONS_VERSION`.
		 * @return Zero on success; -1 on failure.
		 */
		public static extern int git_fetch_options_init(
			git_fetch_options *opts,
			c_uint version);


		/**
		 * Controls the behavior of a git_push object.
		 */
		public struct git_push_options
		{
			c_uint version;

			/**
			 * If the transport being used to push to the remote requires the creation
			 * of a pack file, this controls the number of worker threads used by
			 * the packbuilder when creating that pack file to be sent to the remote.
			 *
			 * If set to 0, the packbuilder will auto-detect the number of threads
			 * to create. The default value is 1.
			 */
			c_uint pb_parallelism;

			/**
			 * Callbacks to use for this push operation
			 */
			git_remote_callbacks callbacks;

			/**
			* Proxy options to use, by default no proxy is used.
			*/
			git_proxy_options proxy_opts;

			/**
			 * Extra headers for this push operation
			 */
			git_strarray custom_headers;
		}

		//#define GIT_PUSH_OPTIONS_VERSION 1
		//#define GIT_PUSH_OPTIONS_INIT { GIT_PUSH_OPTIONS_VERSION, 1, GIT_REMOTE_CALLBACKS_INIT, GIT_PROXY_OPTIONS_INIT }

		/**
		 * Initialize git_push_options structure
		 *
		 * Initializes a `git_push_options` with default values. Equivalent to
		 * creating an instance with `GIT_PUSH_OPTIONS_INIT`.
		 *
		 * @param opts The `git_push_options` struct to initialize.
		 * @param version The struct version; pass `GIT_PUSH_OPTIONS_VERSION`.
		 * @return Zero on success; -1 on failure.
		 */
		public static extern int git_push_options_init(
			git_push_options *opts,
			c_uint version);

		/**
		 * Download and index the packfile
		 *
		 * Connect to the remote if it hasn't been done yet, negotiate with
		 * the remote git which objects are missing, download and index the
		 * packfile.
		 *
		 * The .idx file will be created and both it and the packfile with be
		 * renamed to their final name.
		 *
		 * @param remote the remote
		 * @param refspecs the refspecs to use for this negotiation and
		 * download. Use NULL or an empty array to use the base refspecs
		 * @param opts the options to use for this fetch
		 * @return 0 or an error code
		 */
		 public static extern int git_remote_download(git_remote *remote, git_strarray *refspecs, git_fetch_options *opts);

		/**
		 * Create a packfile and send it to the server
		 *
		 * Connect to the remote if it hasn't been done yet, negotiate with
		 * the remote git which objects are missing, create a packfile with the missing objects and send it.
		 *
		 * @param remote the remote
		 * @param refspecs the refspecs to use for this negotiation and
		 * upload. Use NULL or an empty array to use the base refspecs
		 * @param opts the options to use for this push
		 * @return 0 or an error code
		 */
		public static extern int git_remote_upload(git_remote *remote, git_strarray *refspecs, git_push_options *opts);

		/**
		 * Update the tips to the new state
		 *
		 * @param remote the remote to update
		 * @param reflog_message The message to insert into the reflogs. If
		 * NULL and fetching, the default is "fetch <name>", where <name> is
		 * the name of the remote (or its url, for in-memory remotes). This
		 * parameter is ignored when pushing.
		 * @param callbacks  pointer to the callback structure to use
		 * @param update_fetchhead whether to write to FETCH_HEAD. Pass 1 to behave like git.
		 * @param download_tags what the behaviour for downloading tags is for this fetch. This is
		 * ignored for push. This must be the same value passed to `git_remote_download()`.
		 * @return 0 or an error code
		 */
		public static extern int git_remote_update_tips(
				git_remote *remote,
				git_remote_callbacks *callbacks,
				int update_fetchhead,
				git_remote_autotag_option_t download_tags,
				char8* reflog_message);

		/**
		 * Download new data and update tips
		 *
		 * Convenience function to connect to a remote, download the data,
		 * disconnect and update the remote-tracking branches.
		 *
		 * @param remote the remote to fetch from
		 * @param refspecs the refspecs to use for this fetch. Pass NULL or an
		 *                 empty array to use the base refspecs.
		 * @param opts options to use for this fetch
		 * @param reflog_message The message to insert into the reflogs. If NULL, the
		 *								 default is "fetch"
		 * @return 0 or an error code
		 */
		public static extern int git_remote_fetch(
				git_remote *remote,
				git_strarray *refspecs,
				git_fetch_options *opts,
				char8* reflog_message);

		/**
		 * Prune tracking refs that are no longer present on remote
		 *
		 * @param remote the remote to prune
		 * @param callbacks callbacks to use for this prune
		 * @return 0 or an error code
		 */
		public static extern int git_remote_prune(git_remote *remote, git_remote_callbacks *callbacks);

		/**
		 * Perform a push
		 *
		 * Peform all the steps from a push.
		 *
		 * @param remote the remote to push to
		 * @param refspecs the refspecs to use for pushing. If NULL or an empty
		 *                 array, the configured refspecs will be used
		 * @param opts options to use for this push
		 */
		public static extern int git_remote_push(git_remote *remote,
						git_strarray *refspecs,
						git_push_options *opts);

		/**
		 * Get the statistics structure that is filled in by the fetch operation.
		 */
		public static extern git_indexer_progress * git_remote_stats(git_remote *remote);

		/**
		 * Retrieve the tag auto-follow setting
		 *
		 * @param remote the remote to query
		 * @return the auto-follow setting
		 */
		public static extern git_remote_autotag_option_t git_remote_autotag(git_remote *remote);

		/**
		 * Set the remote's tag following setting.
		 *
		 * The change will be made in the configuration. No loaded remotes
		 * will be affected.
		 *
		 * @param repo the repository in which to make the change
		 * @param remote the name of the remote
		 * @param value the new value to take.
		 */
		public static extern int git_remote_set_autotag(git_repository *repo, char8* remote, git_remote_autotag_option_t value);
		/**
		 * Retrieve the ref-prune setting
		 *
		 * @param remote the remote to query
		 * @return the ref-prune setting
		 */
		public static extern int git_remote_prune_refs(git_remote *remote);

		/**
		 * Give the remote a new name
		 *
		 * All remote-tracking branches and configuration settings
		 * for the remote are updated.
		 *
		 * The new name will be checked for validity.
		 * See `git_tag_create()` for rules about valid names.
		 *
		 * No loaded instances of a the remote with the old name will change
		 * their name or their list of refspecs.
		 *
		 * @param problems non-default refspecs cannot be renamed and will be
		 * stored here for further processing by the caller. Always free this
		 * strarray on successful return.
		 * @param repo the repository in which to rename
		 * @param name the current name of the remote
		 * @param new_name the new name the remote should bear
		 * @return 0, GIT_EINVALIDSPEC, GIT_EEXISTS or an error code
		 */
		public static extern int git_remote_rename(
			git_strarray *problems,
			git_repository *repo,
			char8* name,
			char8* new_name);

		/**
		 * Ensure the remote name is well-formed.
		 *
		 * @param remote_name name to be checked.
		 * @return 1 if the reference name is acceptable; 0 if it isn't
		 */
		public static extern int git_remote_is_valid_name(char8* remote_name);

		/**
		* Delete an existing persisted remote.
		*
		* All remote-tracking branches and configuration settings
		* for the remote will be removed.
		*
		* @param repo the repository in which to act
		* @param name the name of the remote to delete
		* @return 0 on success, or an error code.
		*/
		public static extern int git_remote_delete(git_repository *repo, char8* name);

		/**
		 * Retrieve the name of the remote's default branch
		 *
		 * The default branch of a repository is the branch which HEAD points
		 * to. If the remote does not support reporting this information
		 * directly, it performs the guess as git does; that is, if there are
		 * multiple branches which point to the same commit, the first one is
		 * chosen. If the master branch is a candidate, it wins.
		 *
		 * This function must only be called after connecting.
		 *
		 * @param out the buffern in which to store the reference name
		 * @param remote the remote
		 * @return 0, GIT_ENOTFOUND if the remote does not have any references
		 * or none of them point to HEAD's commit, or an error message.
		 */
		public static extern int git_remote_default_branch(git_buf* outVal, git_remote *remote);

		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// checkout.h
		////////////////////////////////////////////////////////////////////////////////////////////////////////

		/**
		 * Checkout behavior flags
		 *
		 * In libgit2, checkout is used to update the working directory and index
		 * to match a target tree.  Unlike git checkout, it does not move the HEAD
		 * commit for you - use `git_repository_set_head` or the like to do that.
		 *
		 * Checkout looks at (up to) four things: the "target" tree you want to
		 * check out, the "baseline" tree of what was checked out previously, the
		 * working directory for actual files, and the index for staged changes.
		 *
		 * You give checkout one of three strategies for update:
		 *
		 * - `GIT_CHECKOUT_NONE` is a dry-run strategy that checks for conflicts,
		 *   etc., but doesn't make any actual changes.
		 *
		 * - `GIT_CHECKOUT_FORCE` is at the opposite extreme, taking any action to
		 *   make the working directory match the target (including potentially
		 *   discarding modified files).
		 *
		 * - `GIT_CHECKOUT_SAFE` is between these two options, it will only make
		 *   modifications that will not lose changes.
		 *
		 *                         |  target == baseline   |  target != baseline  |
		 *    ---------------------|-----------------------|----------------------|
		 *     workdir == baseline |       no action       |  create, update, or  |
		 *                         |                       |     delete file      |
		 *    ---------------------|-----------------------|----------------------|
		 *     workdir exists and  |       no action       |   conflict (notify   |
		 *       is != baseline    | notify dirty MODIFIED | and cancel checkout) |
		 *    ---------------------|-----------------------|----------------------|
		 *      workdir missing,   | notify dirty DELETED  |     create file      |
		 *      baseline present   |                       |                      |
		 *    ---------------------|-----------------------|----------------------|
		 *
		 * To emulate `git checkout`, use `GIT_CHECKOUT_SAFE` with a checkout
		 * notification callback (see below) that displays information about dirty
		 * files.  The default behavior will cancel checkout on conflicts.
		 *
		 * To emulate `git checkout-index`, use `GIT_CHECKOUT_SAFE` with a
		 * notification callback that cancels the operation if a dirty-but-existing
		 * file is found in the working directory.  This core git command isn't
		 * quite "force" but is sensitive about some types of changes.
		 *
		 * To emulate `git checkout -f`, use `GIT_CHECKOUT_FORCE`.
		 *
		 *
		 * There are some additional flags to modify the behavior of checkout:
		 *
		 * - GIT_CHECKOUT_ALLOW_CONFLICTS makes SAFE mode apply safe file updates
		 *   even if there are conflicts (instead of cancelling the checkout).
		 *
		 * - GIT_CHECKOUT_REMOVE_UNTRACKED means remove untracked files (i.e. not
		 *   in target, baseline, or index, and not ignored) from the working dir.
		 *
		 * - GIT_CHECKOUT_REMOVE_IGNORED means remove ignored files (that are also
		 *   untracked) from the working directory as well.
		 *
		 * - GIT_CHECKOUT_UPDATE_ONLY means to only update the content of files that
		 *   already exist.  Files will not be created nor deleted.  This just skips
		 *   applying adds, deletes, and typechanges.
		 *
		 * - GIT_CHECKOUT_DONT_UPDATE_INDEX prevents checkout from writing the
		 *   updated files' information to the index.
		 *
		 * - Normally, checkout will reload the index and git attributes from disk
		 *   before any operations.  GIT_CHECKOUT_NO_REFRESH prevents this reload.
		 *
		 * - Unmerged index entries are conflicts.  GIT_CHECKOUT_SKIP_UNMERGED skips
		 *   files with unmerged index entries instead.  GIT_CHECKOUT_USE_OURS and
		 *   GIT_CHECKOUT_USE_THEIRS to proceed with the checkout using either the
		 *   stage 2 ("ours") or stage 3 ("theirs") version of files in the index.
		 *
		 * - GIT_CHECKOUT_DONT_OVERWRITE_IGNORED prevents ignored files from being
		 *   overwritten.  Normally, files that are ignored in the working directory
		 *   are not considered "precious" and may be overwritten if the checkout
		 *   target contains that file.
		 *
		 * - GIT_CHECKOUT_DONT_REMOVE_EXISTING prevents checkout from removing
		 *   files or folders that fold to the same name on case insensitive
		 *   filesystems.  This can cause files to retain their existing names
		 *   and write through existing symbolic links.
		 */
		public enum git_checkout_strategy_t : c_int
		{
			GIT_CHECKOUT_NONE = 0, /**< default is a dry run, no actual updates */

			/**
			 * Allow safe updates that cannot overwrite uncommitted data.
			 * If the uncommitted changes don't conflict with the checked out files,
			 * the checkout will still proceed, leaving the changes intact.
			 *
			 * Mutually exclusive with GIT_CHECKOUT_FORCE.
			 * GIT_CHECKOUT_FORCE takes precedence over GIT_CHECKOUT_SAFE.
			 */
			GIT_CHECKOUT_SAFE = (1u << 0),

			/**
			 * Allow all updates to force working directory to look like index.
			 *
			 * Mutually exclusive with GIT_CHECKOUT_SAFE.
			 * GIT_CHECKOUT_FORCE takes precedence over GIT_CHECKOUT_SAFE.
			 */
			GIT_CHECKOUT_FORCE = (1u << 1),


			/** Allow checkout to recreate missing files */
			GIT_CHECKOUT_RECREATE_MISSING = (1u << 2),

			/** Allow checkout to make safe updates even if conflicts are found */
			GIT_CHECKOUT_ALLOW_CONFLICTS = (1u << 4),

			/** Remove untracked files not in index (that are not ignored) */
			GIT_CHECKOUT_REMOVE_UNTRACKED = (1u << 5),

			/** Remove ignored files not in index */
			GIT_CHECKOUT_REMOVE_IGNORED = (1u << 6),

			/** Only update existing files, don't create new ones */
			GIT_CHECKOUT_UPDATE_ONLY = (1u << 7),

			/**
			 * Normally checkout updates index entries as it goes; this stops that.
			 * Implies `GIT_CHECKOUT_DONT_WRITE_INDEX`.
			 */
			GIT_CHECKOUT_DONT_UPDATE_INDEX = (1u << 8),

			/** Don't refresh index/config/etc before doing checkout */
			GIT_CHECKOUT_NO_REFRESH = (1u << 9),

			/** Allow checkout to skip unmerged files */
			GIT_CHECKOUT_SKIP_UNMERGED = (1u << 10),
			/** For unmerged files, checkout stage 2 from index */
			GIT_CHECKOUT_USE_OURS = (1u << 11),
			/** For unmerged files, checkout stage 3 from index */
			GIT_CHECKOUT_USE_THEIRS = (1u << 12),

			/** Treat pathspec as simple list of exact match file paths */
			GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH = (1u << 13),

			/** Ignore directories in use, they will be left empty */
			GIT_CHECKOUT_SKIP_LOCKED_DIRECTORIES = (1u << 18),

			/** Don't overwrite ignored files that exist in the checkout target */
			GIT_CHECKOUT_DONT_OVERWRITE_IGNORED = (1u << 19),

			/** Write normal merge files for conflicts */
			GIT_CHECKOUT_CONFLICT_STYLE_MERGE = (1u << 20),

			/** Include common ancestor data in diff3 format files for conflicts */
			GIT_CHECKOUT_CONFLICT_STYLE_DIFF3 = (1u << 21),

			/** Don't overwrite existing files or folders */
			GIT_CHECKOUT_DONT_REMOVE_EXISTING = (1u << 22),

			/** Normally checkout writes the index upon completion; this prevents that. */
			GIT_CHECKOUT_DONT_WRITE_INDEX = (1u << 23),

			/**
			 * THE FOLLOWING OPTIONS ARE NOT YET IMPLEMENTED
			 */

			/** Recursively checkout submodules with same options (NOT IMPLEMENTED) */
			GIT_CHECKOUT_UPDATE_SUBMODULES = (1u << 16),
			/** Recursively checkout submodules if HEAD moved in super repo (NOT IMPLEMENTED) */
			GIT_CHECKOUT_UPDATE_SUBMODULES_IF_CHANGED = (1u << 17),

		}

		/**
		 * Checkout notification flags
		 *
		 * Checkout will invoke an options notification callback (`notify_cb`) for
		 * certain cases - you pick which ones via `notify_flags`:
		 *
		 * - GIT_CHECKOUT_NOTIFY_CONFLICT invokes checkout on conflicting paths.
		 *
		 * - GIT_CHECKOUT_NOTIFY_DIRTY notifies about "dirty" files, i.e. those that
		 *   do not need an update but no longer match the baseline.  Core git
		 *   displays these files when checkout runs, but won't stop the checkout.
		 *
		 * - GIT_CHECKOUT_NOTIFY_UPDATED sends notification for any file changed.
		 *
		 * - GIT_CHECKOUT_NOTIFY_UNTRACKED notifies about untracked files.
		 *
		 * - GIT_CHECKOUT_NOTIFY_IGNORED notifies about ignored files.
		 *
		 * Returning a non-zero value from this callback will cancel the checkout.
		 * The non-zero return value will be propagated back and returned by the
		 * git_checkout_... call.
		 *
		 * Notification callbacks are made prior to modifying any files on disk,
		 * so canceling on any notification will still happen prior to any files
		 * being modified.
		 */
		public enum git_checkout_notify_t : c_int
		{
			GIT_CHECKOUT_NOTIFY_NONE      = 0,
			GIT_CHECKOUT_NOTIFY_CONFLICT  = (1u << 0),
			GIT_CHECKOUT_NOTIFY_DIRTY     = (1u << 1),
			GIT_CHECKOUT_NOTIFY_UPDATED   = (1u << 2),
			GIT_CHECKOUT_NOTIFY_UNTRACKED = (1u << 3),
			GIT_CHECKOUT_NOTIFY_IGNORED   = (1u << 4),

			GIT_CHECKOUT_NOTIFY_ALL       = 0x0FFFFu
		}

		/** Checkout performance-reporting structure */
		struct git_checkout_perfdata
		{
			c_size mkdir_calls;
			c_size stat_calls;
			c_size chmod_calls;
		}

		/** Checkout notification callback function */
		public function c_int git_checkout_notify_cb(
			git_checkout_notify_t why,
			char8 *path,
			git_diff_file *baseline,
			git_diff_file *target,
			git_diff_file *workdir,
			void *payload);

		/** Checkout progress notification function */
		public function void git_checkout_progress_cb(
			char8 *path,
			c_size completed_steps,
			c_size total_steps,
			void *payload);

		/** Checkout perfdata notification function */
		public function void git_checkout_perfdata_cb(
			git_checkout_perfdata *perfdata,
			void *payload);

		/**
		 * Checkout options structure
		 *
		 * Initialize with `GIT_CHECKOUT_OPTIONS_INIT`. Alternatively, you can
		 * use `git_checkout_options_init`.
		 *
		 */
		public struct git_checkout_options
		{
			public c_uint version; /**< The version */

			public c_uint checkout_strategy; /**< default will be a safe checkout */

			public c_int disable_filters;    /**< don't apply filters like CRLF conversion */
			public c_uint dir_mode;  /**< default is 0755 */
			public c_uint file_mode; /**< default is 0644 or 0755 as dictated by blob */
			public c_int file_open_flags;    /**< default is O_CREAT | O_TRUNC | O_WRONLY */

			public c_uint notify_flags; /**< see `git_checkout_notify_t` above */

			/**
			 * Optional callback to get notifications on specific file states.
			 * @see git_checkout_notify_t
			 */
			public git_checkout_notify_cb notify_cb;

			/** Payload passed to notify_cb */
			public void *notify_payload;

			/** Optional callback to notify the consumer of checkout progress. */
			public git_checkout_progress_cb progress_cb;

			/** Payload passed to progress_cb */
			public void *progress_payload;

			/**
			 * A list of wildmatch patterns or paths.
			 *
			 * By default, all paths are processed. If you pass an array of wildmatch
			 * patterns, those will be used to filter which paths should be taken into
			 * account.
			 *
			 * Use GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH to treat as a simple list.
			 */
			public git_strarray paths;

			/**
			 * The expected content of the working directory; defaults to HEAD.
			 *
			 * If the working directory does not match this baseline information,
			 * that will produce a checkout conflict.
			 */
			public git_tree *baseline;

			/**
			 * Like `baseline` above, though expressed as an index.  This
			 * option overrides `baseline`.
			 */
			public git_index *baseline_index;

			public char8* target_directory; /**< alternative checkout path to workdir */

			public char8* ancestor_label; /**< the name of the common ancestor side of conflicts */
			public char8* our_label; /**< the name of the "our" side of conflicts */
			public char8* their_label; /**< the name of the "their" side of conflicts */

			/** Optional callback to notify the consumer of performance data. */
			public git_checkout_perfdata_cb perfdata_cb;

			/** Payload passed to perfdata_cb */
			public void *perfdata_payload;
		}

		//#define GIT_CHECKOUT_OPTIONS_VERSION 1
		//#define GIT_CHECKOUT_OPTIONS_INIT {GIT_CHECKOUT_OPTIONS_VERSION, GIT_CHECKOUT_SAFE}

		/**
		 * Initialize git_checkout_options structure
		 *
		 * Initializes a `git_checkout_options` with default values. Equivalent to creating
		 * an instance with GIT_CHECKOUT_OPTIONS_INIT.
		 *
		 * @param opts The `git_checkout_options` struct to initialize.
		 * @param version The struct version; pass `GIT_CHECKOUT_OPTIONS_VERSION`.
		 * @return Zero on success; -1 on failure.
		 */
		public static extern int git_checkout_options_init(
			git_checkout_options *opts,
			c_uint version);

		/**
		 * Updates files in the index and the working tree to match the content of
		 * the commit pointed at by HEAD.
		 *
		 * Note that this is _not_ the correct mechanism used to switch branches;
		 * do not change your `HEAD` and then call this method, that would leave
		 * you with checkout conflicts since your working directory would then
		 * appear to be dirty.  Instead, checkout the target of the branch and
		 * then update `HEAD` using `git_repository_set_head` to point to the
		 * branch you checked out.
		 *
		 * @param repo repository to check out (must be non-bare)
		 * @param opts specifies checkout options (may be NULL)
		 * @return 0 on success, GIT_EUNBORNBRANCH if HEAD points to a non
		 *         existing branch, non-zero value returned by `notify_cb`, or
		 *         other error code < 0 (use git_error_last for error details)
		 */
		public static extern int git_checkout_head(
			git_repository *repo,
			git_checkout_options *opts);

		/**
		 * Updates files in the working tree to match the content of the index.
		 *
		 * @param repo repository into which to check out (must be non-bare)
		 * @param index index to be checked out (or NULL to use repository index)
		 * @param opts specifies checkout options (may be NULL)
		 * @return 0 on success, non-zero return value from `notify_cb`, or error
		 *         code < 0 (use git_error_last for error details)
		 */
		public static extern int git_checkout_index(
			git_repository *repo,
			git_index *index,
			git_checkout_options *opts);

		/**
		 * Updates files in the index and working tree to match the content of the
		 * tree pointed at by the treeish.
		 *
		 * @param repo repository to check out (must be non-bare)
		 * @param treeish a commit, tag or tree which content will be used to update
		 * the working directory (or NULL to use HEAD)
		 * @param opts specifies checkout options (may be NULL)
		 * @return 0 on success, non-zero return value from `notify_cb`, or error
		 *         code < 0 (use git_error_last for error details)
		 */
		public static extern int git_checkout_tree(
			git_repository *repo,
			git_object *treeish,
			git_checkout_options *opts);


		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// clone.h
		////////////////////////////////////////////////////////////////////////////////////////////////////////

		/**
		 * Options for bypassing the git-aware transport on clone. Bypassing
		 * it means that instead of a fetch, libgit2 will copy the object
		 * database directory instead of figuring out what it needs, which is
		 * faster. If possible, it will hardlink the files to save space.
		 */
		public enum git_clone_local_t : c_int
		{
			/**
			 * Auto-detect (default), libgit2 will bypass the git-aware
			 * transport for local paths, but use a normal fetch for
			 * `file://` urls.
			 */
			GIT_CLONE_LOCAL_AUTO,
			/**
			 * Bypass the git-aware transport even for a `file://` url.
			 */
			GIT_CLONE_LOCAL,
			/**
			 * Do no bypass the git-aware transport
			 */
			GIT_CLONE_NO_LOCAL,
			/**
			 * Bypass the git-aware transport, but do not try to use
			 * hardlinks.
			 */
			GIT_CLONE_LOCAL_NO_LINKS,
		}

		/**
		 * The signature of a function matching git_remote_create, with an additional
		 * void* as a callback payload.
		 *
		 * Callers of git_clone may provide a function matching this signature to override
		 * the remote creation and customization process during a clone operation.
		 *
		 * @param out the resulting remote
		 * @param repo the repository in which to create the remote
		 * @param name the remote's name
		 * @param url the remote's url
		 * @param payload an opaque payload
		 * @return 0, GIT_EINVALIDSPEC, GIT_EEXISTS or an error code
		 */
		public function int git_remote_create_cb(
			git_remote **outVal,
			git_repository *repo,
			char8* name,
			char8* url,
			void *payload);

		/**
		 * The signature of a function matchin git_repository_init, with an
		 * aditional void * as callback payload.
		 *
		 * Callers of git_clone my provide a function matching this signature
		 * to override the repository creation and customization process
		 * during a clone operation.
		 *
		 * @param out the resulting repository
		 * @param path path in which to create the repository
		 * @param bare whether the repository is bare. This is the value from the clone options
		 * @param payload payload specified by the options
		 * @return 0, or a negative value to indicate error
		 */
		public function int git_repository_create_cb(
			git_repository **outVal,
			char8* path,
			int bare,
			void *payload);

		/**
		 * Clone options structure
		 *
		 * Initialize with `GIT_CLONE_OPTIONS_INIT`. Alternatively, you can
		 * use `git_clone_options_init`.
		 *
		 */
		struct git_clone_options
		{
			public c_uint version;

			/**
			 * These options are passed to the checkout step. To disable
			 * checkout, set the `checkout_strategy` to
			 * `GIT_CHECKOUT_NONE`.
			 */
			public git_checkout_options checkout_opts;

			/**
			 * Options which control the fetch, including callbacks.
			 *
			 * The callbacks are used for reporting fetch progress, and for acquiring
			 * credentials in the event they are needed.
			 */
			public git_fetch_options fetch_opts;

			/**
			 * Set to zero (false) to create a standard repo, or non-zero
			 * for a bare repo
			 */
			public c_int bare;

			/**
			 * Whether to use a fetch or copy the object database.
			 */
			public git_clone_local_t local;

			/**
			 * The name of the branch to checkout. NULL means use the
			 * remote's default branch.
			 */
			public char8* checkout_branch;

			/**
			 * A callback used to create the new repository into which to
			 * clone. If NULL, the 'bare' field will be used to determine
			 * whether to create a bare repository.
			 */
			public git_repository_create_cb repository_cb;

			/**
			 * An opaque payload to pass to the git_repository creation callback.
			 * This parameter is ignored unless repository_cb is non-NULL.
			 */
			public void *repository_cb_payload;

			/**
			 * A callback used to create the git_remote, prior to its being
			 * used to perform the clone operation. See the documentation for
			 * git_remote_create_cb for details. This parameter may be NULL,
			 * indicating that git_clone should provide default behavior.
			 */
			public git_remote_create_cb remote_cb;

			/**
			 * An opaque payload to pass to the git_remote creation callback.
			 * This parameter is ignored unless remote_cb is non-NULL.
			 */
			public void *remote_cb_payload;
		}

		//#define GIT_CLONE_OPTIONS_VERSION 1
		//#define GIT_CLONE_OPTIONS_INIT { GIT_CLONE_OPTIONS_VERSION, \
			//{ GIT_CHECKOUT_OPTIONS_VERSION, GIT_CHECKOUT_SAFE }, \
			//GIT_FETCH_OPTIONS_INIT }

		/**
		 * Initialize git_clone_options structure
		 *
		 * Initializes a `git_clone_options` with default values. Equivalent to creating
		 * an instance with GIT_CLONE_OPTIONS_INIT.
		 *
		 * @param opts The `git_clone_options` struct to initialize.
		 * @param version The struct version; pass `GIT_CLONE_OPTIONS_VERSION`.
		 * @return Zero on success; -1 on failure.
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern int git_clone_options_init(git_clone_options *opts, c_uint version);

		/**
		 * Clone a remote repository.
		 *
		 * By default this creates its repository and initial remote to match
		 * git's defaults. You can use the options in the callback to
		 * customize how these are created.
		 *
		 * @param out pointer that will receive the resulting repository object
		 * @param url the remote repository to clone
		 * @param local_path local directory to clone to
		 * @param options configuration options for the clone.  If NULL, the
		 *        function works as though GIT_OPTIONS_INIT were passed.
		 * @return 0 on success, any non-zero return value from a callback
		 *         function, or a negative value to indicate an error (use
		 *         `git_error_last` for a detailed error message)
		 */
		[CLink, CallingConvention(.Stdcall)]
		public static extern int git_clone(out git_repository *outVal, char8* url, char8* local_path, git_clone_options *options);
	}


}
