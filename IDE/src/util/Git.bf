#if true

using System;

namespace IDE.Util
{
	typealias git_time_t = int64;
	typealias git_off_t = int64;

	class Git
	{
		

		public struct git_repository {}
		public struct git_index {}
		public struct git_note {}
		public struct git_tree {}
		public struct git_cred {}
		public struct git_oid {}
		public struct git_cert {}
		public struct git_push_update {}
		public struct git_transport {}
		public struct git_remote {}

		public function int32 git_transport_certificate_check_cb(git_cert *cert, int32 valid, char8* host, void* payload);
		public function int32 git_packbuilder_progress(
			int32 stage,
			uint32 current,
			uint32 total,
			void* payload);
		public function int32 git_push_transfer_progress(
			uint32 current,
			uint32 total,
			int bytes,
			void* payload);
		public function int32 git_push_negotiation(git_push_update** updates, int len, void *payload);
		public function int32 git_transport_cb(git_transport** outTrans, git_remote *owner, void *param);

		public enum git_fetch_prune_t : int32
        {
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

		public enum git_remote_autotag_option_t : int32
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

		public enum git_proxy_t : int32
        {
			/**
			 * Do not attempt to connect through a proxy
			 *
			 * If built against libcurl, it itself may attempt to connect
			 * to a proxy if the environment variables specify it.
			 */
			GIT_PROXY_NONE,
			/**
			 * Try to auto-detect the proxy from the git configuration.
			 */
			GIT_PROXY_AUTO,
			/**
			 * Connect via the URL given in the options
			 */
			GIT_PROXY_SPECIFIED,
		}

		[CRepr]
		public struct git_proxy_options
        {
			public uint32 version;

			/**
			 * The type of proxy to use, by URL, auto-detect.
			 */
			public git_proxy_t type;

			/**
			 * The URL of the proxy.
			 */
			public char8* url;

			/**
			 * This will be called if the remote host requires
			 * authentication in order to connect to it.
			 *
			 * Returning GIT_PASSTHROUGH will make libgit2 behave as
			 * though this field isn't set.
			 */
			public git_cred_acquire_cb credentials;

			/**
			 * If cert verification fails, this will be called to let the
			 * user make the final decision of whether to allow the
			 * connection to proceed. Returns 1 to allow the connection, 0
			 * to disallow it or a negative value to indicate an error.
			 */
		        public git_transport_certificate_check_cb certificate_check;

			/**
			 * Payload to be provided to the credentials and certificate
			 * check callbacks.
			 */
			public void *payload;
		}

		public enum git_clone_local_t : int32
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

		public function int32 git_repository_create_cb(
			git_repository** outRepo,
			char8* path,
			int32 bare,
			void *payload);

		public function int32 git_remote_create_cb(
			git_remote** outRemote,
			git_repository* repo,
			char8* name,
			char8* url,
			void* payload);

		/** Basic type (loose or packed) of any Git object. */
		public enum git_otype
        {
			GIT_OBJ_ANY = -2,		/**< Object can be any of the following */
			GIT_OBJ_BAD = -1,		/**< Object is invalid. */
			GIT_OBJ__EXT1 = 0,		/**< Reserved for future use. */
			GIT_OBJ_COMMIT = 1,		/**< A commit object. */
			GIT_OBJ_TREE = 2,		/**< A tree (directory listing) object. */
			GIT_OBJ_BLOB = 3,		/**< A file revision object. */
			GIT_OBJ_TAG = 4,		/**< An annotated tag object. */
			GIT_OBJ__EXT2 = 5,		/**< Reserved for future use. */
			GIT_OBJ_OFS_DELTA = 6, /**< A delta, base is given by an offset. */
			GIT_OBJ_REF_DELTA = 7, /**< A delta, base is given by object id. */
		}

		[CRepr]
		public struct git_strarray
        {
			char8** strings;
			int count;
		}


		[CallingConvention(.Stdcall), CLink]
		public static extern void git_strarray_free(git_strarray *array);

		[CallingConvention(.Stdcall), CLink]
		public static extern int32 git_strarray_copy(git_strarray *tgt, git_strarray *src);

		/** Time in a signature */
		[CRepr]
		public struct git_time
        {
			git_time_t time; /**< time in seconds from epoch */
			int32 offset; /**< timezone offset, in minutes */
		}

		/** An action signature (e.g. for committers, taggers, etc) */
		[CRepr]
		public struct git_signature
        {
			char8* name; /**< full name of the author */
			char8* email; /**< email of the author */
			git_time timeWhen; /**< time when the action happened */
		}

		/** Basic type of any Git reference. */
		public enum git_ref_t : int32
        {
			GIT_REF_INVALID = 0, /**< Invalid reference */
			GIT_REF_OID = 1, /**< A reference which points at an object id */
			GIT_REF_SYMBOLIC = 2, /**< A reference which points at another reference */
			GIT_REF_LISTALL = GIT_REF_OID|GIT_REF_SYMBOLIC,
		}

		/** Basic type of any Git branch. */
		public enum git_branch_t : int32
        {
			GIT_BRANCH_LOCAL = 1,
			GIT_BRANCH_REMOTE = 2,
			GIT_BRANCH_ALL = GIT_BRANCH_LOCAL|GIT_BRANCH_REMOTE,
		}

		/** Valid modes for index and tree entries. */
		public enum git_filemode_t : int32
        {
			GIT_FILEMODE_UNREADABLE          = 0000000,
			GIT_FILEMODE_TREE                = 0040000,
			GIT_FILEMODE_BLOB                = 0100644,
			GIT_FILEMODE_BLOB_EXECUTABLE     = 0100755,
			GIT_FILEMODE_LINK                = 0120000,
			GIT_FILEMODE_COMMIT              = 0160000,
		}

		/**
		 * This is passed as the first argument to the callback to allow the
		 * user to see the progress.
		 *
		 * - total_objects: number of objects in the packfile being downloaded
		 * - indexed_objects: received objects that have been hashed
		 * - received_objects: objects which have been downloaded
		 * - local_objects: locally-available objects that have been injected
		 *    in order to fix a thin pack.
		 * - received-bytes: size of the packfile received up to now
		 */
		[CRepr]
		public struct git_transfer_progress
        {
			public uint32 total_objects;
			public uint32 indexed_objects;
			public uint32 received_objects;
			public uint32 local_objects;
			public uint32 total_deltas;
			public uint32 indexed_deltas;
			public int received_bytes;
		}

		public enum git_remote_completion_type : int32
        {
			GIT_REMOTE_COMPLETION_DOWNLOAD,
			GIT_REMOTE_COMPLETION_INDEXING,
			GIT_REMOTE_COMPLETION_ERROR,
		}

		/**
		 * Type for progress callbacks during indexing.  Return a value less than zero
		 * to cancel the transfer.
		 *
		 * @param stats Structure containing information about the state of the transfer
		 * @param payload Payload provided by caller
		 */
		public function int32 git_transfer_progress_cb(git_transfer_progress *stats, void* payload);

		/**
		 * Type for messages delivered by the transport.  Return a negative value
		 * to cancel the network operation.
		 *
		 * @param str The message from the transport
		 * @param len The length of the message
		 * @param payload Payload provided by the caller
		 */
		public function int32 git_transport_message_cb(char8* str, int32 len, void *payload);

		public function int git_cred_acquire_cb(
			git_cred **cred,
			char8 *url,
			char8 *username_from_url,
			uint32 allowed_types,
			void *payload);

		public enum git_checkout_notify_t : int32
        {
			GIT_CHECKOUT_NOTIFY_NONE      = 0,
			GIT_CHECKOUT_NOTIFY_CONFLICT  = (1u << 0),
			GIT_CHECKOUT_NOTIFY_DIRTY     = (1u << 1),
			GIT_CHECKOUT_NOTIFY_UPDATED   = (1u << 2),
			GIT_CHECKOUT_NOTIFY_UNTRACKED = (1u << 3),
			GIT_CHECKOUT_NOTIFY_IGNORED   = (1u << 4),

			GIT_CHECKOUT_NOTIFY_ALL       = 0x0FFFFu
		}

		[CRepr]
		public struct git_checkout_perfdata
        {
			public int mkdir_calls;
			public int stat_calls;
			public int chmod_calls;
		}

		public struct git_diff_file
        {
			/*git_oid     id;
			const char *path;
			git_off_t   size;
			uint32_t    flags;
			uint16_t    mode;
			uint16_t    id_abbrev;*/
		}

		/** Checkout notification callback function */
		public function int32 git_checkout_notify_cb(
			git_checkout_notify_t why,
			char8* path,
			git_diff_file* baseline,
			git_diff_file* target,
			git_diff_file* workdir,
			void *payload);

		/** Checkout progress notification function */
		public function void git_checkout_progress_cb(
			char8* path,
			int completed_steps,
			int total_steps,
			void *payload);

		/** Checkout perfdata notification function */
		public function void git_checkout_perfdata_cb(
			git_checkout_perfdata *perfdata,
			void *payload);

		[CRepr]
		public struct git_checkout_options
		{
            public uint32 version;

			public uint32 checkout_strategy; /**< default will be a dry run */

			public int32 disable_filters;    /**< don't apply filters like CRLF conversion */
			public uint32 dir_mode;  /**< default is 0755 */
			public uint32 file_mode; /**< default is 0644 or 0755 as dictated by blob */
			public int32 file_open_flags;    /**< default is O_CREAT | O_TRUNC | O_WRONLY */

			public uint32 notify_flags; /**< see `git_checkout_notify_t` above */
			public git_checkout_notify_cb notify_cb;
			public void *notify_payload;

			/** Optional callback to notify the consumer of checkout progress. */
			public git_checkout_progress_cb progress_cb;
			public void *progress_payload;

			/** When not zeroed out, array of fnmatch patterns specifying which
			 *  paths should be taken into account, otherwise all files.  Use
			 *  GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH to treat as simple list.
			 */
			public git_strarray paths;

			/** The expected content of the working directory; defaults to HEAD.
			 *  If the working directory does not match this baseline information,
			 *  that will produce a checkout conflict.
			 */
			public git_tree* baseline;

			/** Like `baseline` above, though expressed as an index.  This
			 *  option overrides `baseline`.
			 */
			public git_index* baseline_index; /**< expected content of workdir, expressed as an index. */

			public char8* target_directory; /**< alternative checkout path to workdir */

			public char8* ancestor_label; /**< the name of the common ancestor side of conflicts */
			public char8* our_label; /**< the name of the "our" side of conflicts */
			public char8* their_label; /**< the name of the "their" side of conflicts */

			/** Optional callback to notify the consumer of performance data. */
			public git_checkout_perfdata_cb perfdata_cb;
			public void *perfdata_payload;
		}

		/**
		 * The callback settings structure
		 *
		 * Set the callbacks to be called by the remote when informing the user
		 * about the progress of the network operations.
		 */
		[CRepr]
		public struct git_remote_callbacks
        {
			public uint32 version;
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
			public function int32(git_remote_completion_type type, void *data) completion;

			/**
			 * This will be called if the remote host requires
			 * authentication in order to connect to it.
			 *
			 * Returning GIT_PASSTHROUGH will make libgit2 behave as
			 * though this field isn't set.
			 */
			public git_cred_acquire_cb credentials;

			/**
			 * If cert verification fails, this will be called to let the
			 * user make the final decision of whether to allow the
			 * connection to proceed. Returns 1 to allow the connection, 0
			 * to disallow it or a negative value to indicate an error.
			 */
			public git_transport_certificate_check_cb certificate_check;

			/**
			 * During the download of new data, this will be regularly
			 * called with the current count of progress done by the
			 * indexer.
			 */
			public git_transfer_progress_cb transfer_progress;

			/**
			 * Each time a reference is updated locally, this function
			 * will be called with information about it.
			 */
			public function int32 (char8* refname, git_oid *a, git_oid *b, void *data) update_tips;

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
			public git_push_transfer_progress push_transfer_progress;

			/**
			 * Called for each updated reference on push. If `status` is
			 * not `NULL`, the update was rejected by the remote server
			 * and `status` contains the reason given.
			 */
			public function int32 (char8* refname, char8* status, void *data) push_update_reference;

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
		}

		/**
		 * Fetch options structure.
		 *
		 * Zero out for defaults.  Initialize with `GIT_FETCH_OPTIONS_INIT` macro to
		 * correctly set the `version` field.  E.g.
		 *
		 *		git_fetch_options opts = GIT_FETCH_OPTIONS_INIT;
		 */
		[CRepr]
		public struct git_fetch_options
        {
			public int32 version;

			/**
			 * Callbacks to use for this fetch operation
			 */
			public git_remote_callbacks callbacks;

			/**
			 * Whether to perform a prune after the fetch
			 */
			public git_fetch_prune_t prune;

			/**
			 * Whether to write the results to FETCH_HEAD. Defaults to
			 * on. Leave this default in order to behave like git.
			 */
			public int32 update_fetchhead;

			/**
			 * Determines how to behave regarding tags on the remote, such
			 * as auto-downloading tags for objects we're downloading or
			 * downloading all of them.
			 *
			 * The default is to auto-follow tags.
			 */
			public git_remote_autotag_option_t download_tags;

			/**
			 * Proxy options to use, by default no proxy is used.
			 */
			public git_proxy_options proxy_opts;

			/**
			 * Extra headers for this fetch operation
			 */
			public git_strarray custom_headers;
		}

		[CRepr]
		public struct git_clone_options
		{
			public uint32 version;

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
			public int32 bare;

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
			public void* repository_cb_payload;

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
			public void* remote_cb_payload;
		}

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 git_libgit2_init();

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 git_libgit2_shutdown();

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 git_clone(git_repository** repoOut, char8* url, char8* local_path, git_clone_options* options);

		[CLink, CallingConvention(.Stdcall)]
		public static extern void git_repository_free(git_repository *repo);
	}
}

#endif