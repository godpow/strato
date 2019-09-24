package emu.skyline;

import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.FileObserver;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.ListView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.ActionBar;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.SearchView;
import androidx.preference.PreferenceManager;

import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.stream.Collectors;

import javax.net.ssl.HttpsURLConnection;

import static java.lang.Thread.interrupted;

public class LogActivity extends AppCompatActivity {
    private File log_file;
    private BufferedReader reader;
    private Thread thread;
    private LogAdapter adapter;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.log_activity);
        setSupportActionBar(findViewById(R.id.toolbar));
        ActionBar actionBar = getSupportActionBar();
        if (actionBar != null)
            actionBar.setDisplayHomeAsUpEnabled(true);
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        final ListView log_list = this.findViewById(R.id.log_list);
        adapter = new LogAdapter(this, Integer.parseInt(prefs.getString("log_level", "3")), getResources().getStringArray(R.array.log_level));
        log_list.setAdapter(adapter);
        log_file = new File(getApplicationInfo().dataDir + "/skyline.log");
        try {
            InputStream inputStream = new FileInputStream(log_file);
            reader = new BufferedReader(new InputStreamReader(inputStream));
            thread = new Thread(() -> {
                // Required as FileObserver(File) is only on API level 29 also no AndroidX version present
                        FileObserver observer = new FileObserver(log_file.getPath()) {
                    @Override
                    public void onEvent(int event, String path) {
                        if (event == FileObserver.MODIFY) {
                            try {
                                boolean done = false;
                                while (!done) {
                                    final String line = reader.readLine();
                                    done = (line == null);
                                    if (!done) {
                                        runOnUiThread(() -> adapter.add(line));
                                    }
                                }
                            } catch (IOException e) {
                                Log.w("Logger", "IO Error during access of log file: " + e.getMessage());
                                Toast.makeText(getApplicationContext(), getString(R.string.io_error) + ": " + e.getMessage(), Toast.LENGTH_LONG).show();
                            }
                        }
                    }
                };
                observer.onEvent(FileObserver.MODIFY, log_file.getPath());
                observer.startWatching();
                while (!interrupted()) ;
                observer.stopWatching();
            });
            thread.start();
        } catch (FileNotFoundException e) {
            Log.w("Logger", "IO Error during access of log file: " + e.getMessage());
            Toast.makeText(getApplicationContext(), getString(R.string.file_missing), Toast.LENGTH_LONG).show();
            finish();
        }
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.toolbar_log, menu);
        MenuItem mSearch = menu.findItem(R.id.action_search_log);
        final SearchView searchView = (SearchView) mSearch.getActionView();
        searchView.setSubmitButtonEnabled(false);
        searchView.setOnQueryTextListener(new SearchView.OnQueryTextListener() {
            public boolean onQueryTextSubmit(String query) {
                searchView.setIconified(false);
                return false;
            }

            @Override
            public boolean onQueryTextChange(String newText) {
                adapter.getFilter().filter(newText);
                return true;
            }
        });
        return super.onCreateOptionsMenu(menu);
    }

    @Override
    public boolean onOptionsItemSelected(@NonNull MenuItem item) {
        switch (item.getItemId()) {
            case R.id.action_clear:
                try {
                    FileWriter fileWriter = new FileWriter(log_file, false);
                    fileWriter.close();
                } catch (IOException e) {
                    Log.w("Logger", "IO Error while clearing the log file: " + e.getMessage());
                    Toast.makeText(getApplicationContext(), getString(R.string.io_error) + ": " + e.getMessage(), Toast.LENGTH_LONG).show();
                }
                Toast.makeText(getApplicationContext(), getString(R.string.cleared), Toast.LENGTH_LONG).show();
                finish();
                return true;
            case R.id.action_share_log:
                Thread share_thread = new Thread(() -> {
                    HttpsURLConnection urlConnection = null;
                    try {
                        URL url = new URL("https://hastebin.com/documents");
                        urlConnection = (HttpsURLConnection) url.openConnection();
                        urlConnection.setRequestMethod("POST");
                        urlConnection.setRequestProperty("Host", "hastebin.com");
                        urlConnection.setRequestProperty("Content-Type", "application/json; charset=utf-8");
                        urlConnection.setRequestProperty("Referer", "https://hastebin.com/");
                        urlConnection.setRequestProperty("Connection", "keep-alive");
                        OutputStream outputStream = new BufferedOutputStream(urlConnection.getOutputStream());
                        BufferedWriter bufferedWriter = new BufferedWriter(new OutputStreamWriter(outputStream, StandardCharsets.UTF_8));
                        FileReader fileReader = new FileReader(log_file);
                        int chr;
                        while ((chr = fileReader.read()) != -1) {
                            bufferedWriter.write(chr);
                        }
                        bufferedWriter.flush();
                        bufferedWriter.close();
                        outputStream.close();
                        if (urlConnection.getResponseCode() != 200) {
                            Log.e("LogUpload", "HTTPS Status Code: " + urlConnection.getResponseCode());
                            throw new Exception();
                        }
                        InputStream inputStream = new BufferedInputStream(urlConnection.getInputStream());
                        BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(inputStream, StandardCharsets.UTF_8));
                        String key = new JSONObject(bufferedReader.lines().collect(Collectors.joining())).getString("key");
                        bufferedReader.close();
                        inputStream.close();
                        String result = "https://hastebin.com/" + key;
                        Intent sharingIntent = new Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, result);
                        startActivity(Intent.createChooser(sharingIntent, "Share log url with:"));
                    } catch (Exception e) {
                        runOnUiThread(() -> Toast.makeText(getApplicationContext(), getString(R.string.share_error), Toast.LENGTH_LONG).show());
                        e.printStackTrace();
                    } finally {
                        assert urlConnection != null;
                        urlConnection.disconnect();
                    }
                });
                share_thread.start();
                try {
                    share_thread.join(1000);
                } catch (Exception e) {
                    Toast.makeText(getApplicationContext(), getString(R.string.share_error), Toast.LENGTH_LONG).show();
                    e.printStackTrace();
                }
            default:
                return super.onOptionsItemSelected(item);
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        try {
            thread.interrupt();
            thread.join();
            reader.close();
        } catch (IOException e) {
            Log.w("Logger", "IO Error during closing BufferedReader: " + e.getMessage());
            Toast.makeText(getApplicationContext(), getString(R.string.io_error) + ": " + e.getMessage(), Toast.LENGTH_LONG).show();
        } catch (NullPointerException | InterruptedException ignored) {
        }
    }
}
