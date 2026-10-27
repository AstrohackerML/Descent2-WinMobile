package astrohacker.descent2;

import android.annotation.TargetApi;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.res.AssetFileDescriptor;
import android.content.res.Resources;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.media.MediaPlayer;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.DisplayMetrics;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Display;
import android.view.Gravity;
import android.view.Surface;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * Created by devin on 4/17/16.
 */
public class DescentActivity extends Activity implements SensorEventListener {
	private static final int PICK_DATA_FOLDER = 1;

	private DescentView descentView;
	private MediaPlayer mediaPlayer;
	private Sensor gyroscopeSensor;
	private SensorManager sensorManager;
	private float buttonSizeBias;
	private float acceleration[];
	private int mediaPlayerPosition;
	private int refreshPeriodUs;
	private String dataPath;
	private Button chooseButton, checkButton;
	private ProgressBar copyProgress;
	private TextView dataStatus;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		DisplayMetrics metrics;
		Resources resources;

		super.onCreate(savedInstanceState);

		// Enable immersive mode and make sure it's enabled whenever we're fullscreen
		setImmersive();
		if (Build.VERSION.SDK_INT >= 19) {
			getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(
					new View.OnSystemUiVisibilityChangeListener() {
						@Override
						public void onSystemUiVisibilityChange(int visibility) {
							if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
								setImmersive();
							}
						}
					});
		}

		// Calculate button size bias; want slightly bigger buttons on bigger screens
		resources = getResources();
		metrics = resources.getDisplayMetrics();
		buttonSizeBias = (float) Math.min(Math.max((metrics.widthPixels / metrics.xdpi
				+ metrics.heightPixels / metrics.ydpi) / 5.5f, 1), 1.4);

		// Set up gyroscope
		sensorManager = (SensorManager) getSystemService(SENSOR_SERVICE);
		gyroscopeSensor = sensorManager.getDefaultSensor(Sensor.TYPE_GYROSCOPE);

		// If we're on SDK 11, absolute values are supported for gyro refresh period. Use the screen's refresh rate to
		// determine refresh period.
		if (Build.VERSION.SDK_INT >= 11) {
			final Display display = ((WindowManager) getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();
			final float refreshRate = display.getRefreshRate();
			refreshPeriodUs = (int) (1.0 / refreshRate * 1000000);
		}

		// Create media player for MIDI
		mediaPlayer = new MediaPlayer();

		// Keep the screen from going to sleep
		getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

		// The game data files are not part of the app; ask for them if we don't have them yet
		dataPath = GameData.findDataPath(this);
		if (dataPath != null) {
			startGame();
		} else {
			showDataSetup();
		}
	}

	/**
	 * Folder with the game data files, or "" if they are in the APK's assets
	 */
	public String getDataPath() {
		return dataPath;
	}

	private void startGame() {
		// Create OpenGL ES view
		descentView = new DescentView(this);

		// Set views
		setContentView(descentView);
	}

	private void showDataSetup() {
		LinearLayout layout = new LinearLayout(this);
		ScrollView scrollView = new ScrollView(this);
		TextView title = new TextView(this);
		TextView message = new TextView(this);
		int padding = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 24,
				getResources().getDisplayMetrics());
		File manualDir = GameData.manualDir(this);

		layout.setOrientation(LinearLayout.VERTICAL);
		layout.setGravity(Gravity.CENTER_HORIZONTAL);
		layout.setPadding(padding, padding, padding, padding);

		title.setText(R.string.data_title);
		title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 24);
		layout.addView(title);

		message.setText(R.string.data_message);
		message.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
		message.setPadding(0, padding / 2, 0, padding / 2);
		if (Build.VERSION.SDK_INT < 21 && manualDir != null) {
			message.append("\n\n" + getString(R.string.data_message_manual,
					manualDir.getAbsolutePath()));
		}
		layout.addView(message);

		chooseButton = new Button(this);
		chooseButton.setText(R.string.data_choose);
		chooseButton.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				pickDataFolder();
			}
		});
		if (Build.VERSION.SDK_INT >= 21) {
			layout.addView(chooseButton);
		}

		checkButton = new Button(this);
		checkButton.setText(R.string.data_check);
		checkButton.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				checkData();
			}
		});
		layout.addView(checkButton);

		copyProgress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
		copyProgress.setVisibility(View.GONE);
		layout.addView(copyProgress, new LinearLayout.LayoutParams(
				LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

		dataStatus = new TextView(this);
		dataStatus.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
		dataStatus.setPadding(0, padding / 2, 0, 0);
		layout.addView(dataStatus);

		scrollView.addView(layout);
		setContentView(scrollView);
	}

	private void checkData() {
		dataPath = GameData.findDataPath(this);
		if (dataPath != null) {
			startGame();
		} else {
			File manualDir = GameData.manualDir(this);
			List<String> missing = GameData.missingFiles(manualDir != null ? manualDir
					: GameData.copyDir(this));
			dataStatus.setText(getString(R.string.data_missing, TextUtils.join(", ", missing)));
		}
	}

	@TargetApi(21)
	private void pickDataFolder() {
		Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);

		// Older pickers hide the device's own storage unless asked to show it
		intent.putExtra("android.content.extra.SHOW_ADVANCED", true);
		startActivityForResult(intent, PICK_DATA_FOLDER);
	}

	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);
		if (requestCode == PICK_DATA_FOLDER && resultCode == RESULT_OK && data != null
				&& data.getData() != null) {
			copyData(data.getData());
		}
	}

	/**
	 * Copies the game data from the picked folder on a background thread, then starts the game
	 */
	private void copyData(final Uri tree) {
		chooseButton.setEnabled(false);
		checkButton.setEnabled(false);
		copyProgress.setProgress(0);
		copyProgress.setVisibility(View.VISIBLE);
		dataStatus.setText("");

		new Thread(new Runnable() {
			private long lastUpdate;

			@Override
			public void run() {
				String error = null;
				List<String> missing = null;

				try {
					missing = GameData.copyFromTree(DescentActivity.this, tree,
							new GameData.Progress() {
								@Override
								public void onProgress(final long copied, final long total) {
									long now = System.currentTimeMillis();
									if (now - lastUpdate < 100 && copied < total) {
										return;
									}
									lastUpdate = now;
									runOnUiThread(new Runnable() {
										@Override
										public void run() {
											copyProgress.setProgress((int) (copied * 100 / total));
											dataStatus.setText(getString(R.string.data_copying,
													(int) (copied >> 20), (int) (total >> 20)));
										}
									});
								}
							});
				} catch (IOException | RuntimeException e) {
					error = e.getMessage() != null ? e.getMessage() : e.toString();
				}

				final String finalError = error;
				final List<String> finalMissing = missing;
				runOnUiThread(new Runnable() {
					@Override
					public void run() {
						if (isFinishing()) {
							return;
						}
						chooseButton.setEnabled(true);
						checkButton.setEnabled(true);
						copyProgress.setVisibility(View.GONE);
						if (finalError != null) {
							dataStatus.setText(getString(R.string.data_failed, finalError));
						} else if (!finalMissing.isEmpty()) {
							dataStatus.setText(getString(R.string.data_missing,
									TextUtils.join(", ", finalMissing)));
						} else {
							checkData();
						}
					}
				});
			}
		}).start();
	}

	@Override
	public void onBackPressed() {
		if (descentView == null) {
			super.onBackPressed();
			return;
		}
		// Don't let Back finish the activity: Descent would keep running in this
		// process and could not be started again. Treat it as Escape instead;
		// the game is left through its own Quit menu item.
		descentView.sendEscape();
	}

	@Override
	protected void onPause() {
		super.onPause();
		descentPause();
		mediaPlayer.pause();
		mediaPlayerPosition = mediaPlayer.getCurrentPosition();
		stopMotion();
	}

	@Override
	protected void onResume() {
		super.onResume();
		setImmersive();
		if (descentView != null && !descentView.getSurfaceWasDestroyed()) {
			descentView.resumeRenderThread();
		}
		mediaPlayer.seekTo(mediaPlayerPosition);
		mediaPlayer.start();
		if (getUseGyroscope()) {
			startMotion();
		}
	}

	@Override
	public void onSensorChanged(SensorEvent event) {
		acceleration = event.values;
	}

	@Override
	public void onAccuracyChanged(Sensor sensor, int accuracy) {

	}

	@SuppressWarnings("unused")
	private float[] getRotationRate() {
		if (haveGyroscope()) {
			if (getWindowManager().getDefaultDisplay().getRotation() == Surface.ROTATION_90) {
				acceleration[0] *= -1;
				acceleration[1] *= -1;
			}
			return acceleration;
		} else {
			return new float[]{0, 0, 0};
		}
	}

	private boolean haveGyroscope() {
		return gyroscopeSensor != null;
	}

	private void startMotion() {
		// According to this, API 11 can specify the delay as an absolute value
		// Some devices have high refresh rate displays, so a lower delay is needed to improve gyro responsiveness
		// https://developer.android.com/guide/topics/sensors/sensors_overview
		if (Build.VERSION.SDK_INT >= 11) {
			sensorManager.registerListener(this, gyroscopeSensor, refreshPeriodUs);
		} else {
			sensorManager.registerListener(this, gyroscopeSensor, SensorManager.SENSOR_DELAY_GAME);
		}
	}

	private void stopMotion() {
		sensorManager.unregisterListener(this);
	}

	@SuppressWarnings("unused")
	private void playMidi(String path, boolean looping) {
		File file = new File(path);
		FileDescriptor fd;
		FileInputStream fos;

		try {
			fos = new FileInputStream(file);
			fd = fos.getFD();
			mediaPlayer.setDataSource(fd);
			mediaPlayer.prepare();
		} catch (IOException e) {
			e.printStackTrace();
		}
		mediaPlayer.setLooping(looping);
		mediaPlayer.start();
	}

	/**
	 * CD audio tracks copied with the game data, or null if there are none
	 */
	private File musicDir() {
		File dir;

		if (dataPath == null || dataPath.isEmpty()) {
			return null;
		}
		dir = new File(dataPath, GameData.MUSIC_DIR);
		return dir.isDirectory() ? dir : null;
	}

	/**
	 * Names of the CD audio tracks, sorted, from the data folder or else the APK's assets
	 */
	private String[] listRedbookTracks() {
		File dir = musicDir();
		String trackList[] = null;

		if (dir != null) {
			trackList = dir.list();
		}
		if (trackList == null || trackList.length == 0) {
			try {
				trackList = getAssets().list(GameData.MUSIC_DIR);
			} catch (IOException e) {
				trackList = null;
			}
		}
		if (trackList == null) {
			return new String[0];
		}
		Arrays.sort(trackList);
		return trackList;
	}

	@SuppressWarnings("unused")
	private boolean playRedbookTrack(int tracknum, boolean looping) {
		File dir = musicDir();
		String trackName = null;

		for (String track : listRedbookTracks()) {
			if (track.matches(String.format(Locale.ROOT, "%02d.*", tracknum))) {
				trackName = track;
				break;
			}
		}
		if (trackName == null) {
			return false;
		}
		try {
			if (dir != null && new File(dir, trackName).isFile()) {
				mediaPlayer.setDataSource(new File(dir, trackName).getAbsolutePath());
			} else {
				AssetFileDescriptor fd = getAssets().openFd(GameData.MUSIC_DIR + "/" + trackName);
				mediaPlayer.setDataSource(fd.getFileDescriptor(), fd.getStartOffset(), fd.getLength());
			}
			mediaPlayer.prepare();
		} catch (IOException e) {
			return false;
		}
		mediaPlayer.setLooping(looping);
		mediaPlayer.start();
		return true;
	}

	@SuppressWarnings("unused")
	private int getRedbookTrackCount() {
		String trackList[] = listRedbookTracks();

		try {
			return Integer.parseInt(trackList[trackList.length - 1].substring(0, 2));
		} catch (Exception e) {
			return 0;
		}
	}

	@SuppressWarnings("unused")
	private void stopMusic() {
		mediaPlayer.stop();
		mediaPlayer.reset();
	}

	@SuppressWarnings("unused")
	private void setMusicVolume(float volume) {
		mediaPlayer.setVolume(volume, volume);
	}

	@SuppressWarnings("unused")
	private float dpToPx(float dp) {
		Resources resources = getResources();
		DisplayMetrics metrics = resources.getDisplayMetrics();
		return dp * (((float) metrics.densityDpi / DisplayMetrics.DENSITY_DEFAULT) * buttonSizeBias);
	}

	@SuppressWarnings("unused")
	private float pxToDp(float px) {
		Resources resources = getResources();
		DisplayMetrics metrics = resources.getDisplayMetrics();
		return px / (((float) metrics.densityDpi / DisplayMetrics.DENSITY_DEFAULT) * buttonSizeBias);
	}

	/**
	 * Enables immersive mode, hiding navigation controls
	 */
	private void setImmersive() {
		if (Build.VERSION.SDK_INT >= 19) {
			getWindow().getDecorView().setSystemUiVisibility(
					View.SYSTEM_UI_FLAG_LAYOUT_STABLE
							| View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
							| View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
							| View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
							| View.SYSTEM_UI_FLAG_FULLSCREEN
							| View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
		}
	}

	private static native void descentPause();

	private static native boolean getUseGyroscope();

	static {
		System.loadLibrary("descent2");
	}
}
