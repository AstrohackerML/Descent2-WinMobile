package astrohacker.descent2;

import android.annotation.TargetApi;
import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Finds the Descent II data files and copies them from a folder the player picks.
 */
final class GameData {
	/** Files the game cannot start without */
	private static final String[] REQUIRED = {
			"descent2.hog", "descent2.ham", "alien1.pig", "alien2.pig", "fire.pig", "groupa.pig",
			"ice.pig", "water.pig"
	};

	/** Either of these sound files will do */
	private static final String[] SOUNDS = {"descent2.s22", "descent2.s11"};

	/** Copy every file with one of these extensions: data, movies and add-on missions */
	private static final String[] EXTENSIONS = {".hog", ".ham", ".pig", ".s11", ".s22", ".mvl", ".mn2"};

	/** Folder with the CD audio tracks, named <tracknum>.<ext> */
	static final String MUSIC_DIR = "music";

	/** How deep to look into subfolders of the picked folder */
	private static final int MAX_DEPTH = 3;

	interface Progress {
		void onProgress(long copied, long total);
	}

	private GameData() {
	}

	/**
	 * Folder the picked data files are copied to
	 */
	static File copyDir(Context context) {
		return new File(context.getFilesDir(), "data");
	}

	/**
	 * Folder the player can copy the data files to by hand (over USB)
	 */
	static File manualDir(Context context) {
		return context.getExternalFilesDir(null);
	}

	/**
	 * Returns the folder with complete game data, "" if the data is bundled in the APK's
	 * assets, or null if there is no data yet
	 */
	static String findDataPath(Context context) {
		File manual = manualDir(context);

		if (missingFiles(copyDir(context)).isEmpty()) {
			return copyDir(context).getAbsolutePath();
		}
		if (manual != null && missingFiles(manual).isEmpty()) {
			return manual.getAbsolutePath();
		}
		if (inAssets(context)) {
			return "";
		}
		return null;
	}

	/**
	 * Names of the required files the folder does not have
	 */
	static List<String> missingFiles(File dir) {
		List<String> missing = new ArrayList<>();

		for (String name : REQUIRED) {
			if (!new File(dir, name).isFile()) {
				missing.add(name);
			}
		}
		if (!new File(dir, SOUNDS[0]).isFile() && !new File(dir, SOUNDS[1]).isFile()) {
			missing.add(SOUNDS[0]);
		}
		return missing;
	}

	private static boolean inAssets(Context context) {
		try {
			List<String> assets = Arrays.asList(context.getAssets().list(""));
			for (String name : REQUIRED) {
				if (!assets.contains(name)) {
					return false;
				}
			}
			return assets.contains(SOUNDS[0]) || assets.contains(SOUNDS[1]);
		} catch (IOException e) {
			return false;
		}
	}

	/**
	 * Copies the data files found in a folder picked with ACTION_OPEN_DOCUMENT_TREE into
	 * copyDir(), along with the CD audio tracks from a "music" folder, if there is one.
	 * File names are lowercased, since the game opens them in lower case.
	 *
	 * @return the required files that are still missing afterwards
	 */
	@TargetApi(21)
	static List<String> copyFromTree(Context context, Uri tree, Progress progress)
			throws IOException {
		ContentResolver resolver = context.getContentResolver();
		Map<String, Uri> files = new LinkedHashMap<>();
		Map<String, Long> sizes = new LinkedHashMap<>();
		File dir = copyDir(context);
		byte[] buffer = new byte[1 << 20];
		long total = 0, copied = 0;

		findFiles(resolver, tree, DocumentsContract.getTreeDocumentId(tree), 0, false, files,
				sizes);
		if (files.isEmpty()) {
			return missingFiles(dir);
		}
		for (long size : sizes.values()) {
			total += size;
		}
		if (!dir.isDirectory() && !dir.mkdirs()) {
			throw new IOException("Cannot create " + dir);
		}
		if (dir.getUsableSpace() < total) {
			throw new IOException(context.getString(R.string.data_no_space,
					total / (1024 * 1024)));
		}

		for (Map.Entry<String, Uri> file : files.entrySet()) {
			File part = new File(dir, file.getKey() + ".part");
			File parent = part.getParentFile();
			if (!parent.isDirectory() && !parent.mkdirs()) {
				throw new IOException("Cannot create " + parent);
			}
			InputStream in = resolver.openInputStream(file.getValue());
			if (in == null) {
				throw new IOException("Cannot open " + file.getKey());
			}
			try {
				OutputStream out = new FileOutputStream(part);
				try {
					int n;
					while ((n = in.read(buffer)) > 0) {
						out.write(buffer, 0, n);
						copied += n;
						progress.onProgress(copied, total);
					}
				} finally {
					out.close();
				}
			} finally {
				in.close();
			}
			if (!part.renameTo(new File(dir, file.getKey()))) {
				throw new IOException("Cannot rename " + part);
			}
		}
		return missingFiles(dir);
	}

	@TargetApi(21)
	private static void findFiles(ContentResolver resolver, Uri tree, String parentId, int depth,
								  boolean inMusic, Map<String, Uri> files, Map<String, Long> sizes) {
		Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
		String[] columns = {
				DocumentsContract.Document.COLUMN_DOCUMENT_ID,
				DocumentsContract.Document.COLUMN_DISPLAY_NAME,
				DocumentsContract.Document.COLUMN_MIME_TYPE,
				DocumentsContract.Document.COLUMN_SIZE
		};
		Map<String, Boolean> subdirs = new LinkedHashMap<>();
		Cursor cursor = resolver.query(children, columns, null, null, null);

		if (cursor == null) {
			return;
		}
		try {
			while (cursor.moveToNext()) {
				String id = cursor.getString(0);
				String name = cursor.getString(1).toLowerCase(Locale.ROOT);
				String key = null;
				if (DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2))) {
					subdirs.put(id, inMusic || name.equals(MUSIC_DIR));
				} else if (inMusic && name.matches("[0-9][0-9].*")) {
					key = MUSIC_DIR + "/" + name;
				} else if (!inMusic && isDataFile(name)) {
					key = name;
				}
				if (key != null && !files.containsKey(key)) {
					files.put(key, DocumentsContract.buildDocumentUriUsingTree(tree, id));
					sizes.put(key, cursor.getLong(3));
				}
			}
		} finally {
			cursor.close();
		}
		if (depth < MAX_DEPTH) {
			for (Map.Entry<String, Boolean> subdir : subdirs.entrySet()) {
				findFiles(resolver, tree, subdir.getKey(), depth + 1, subdir.getValue(), files,
						sizes);
			}
		}
	}

	private static boolean isDataFile(String name) {
		for (String extension : EXTENSIONS) {
			if (name.endsWith(extension)) {
				return true;
			}
		}
		return false;
	}
}
