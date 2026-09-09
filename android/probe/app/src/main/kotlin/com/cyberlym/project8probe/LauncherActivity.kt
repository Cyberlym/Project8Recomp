package com.cyberlym.project8probe

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.io.File
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.Files
import java.nio.file.StandardCopyOption

class LauncherActivity : ComponentActivity() {
    private var gameImageState by mutableStateOf(GameImageUiState())
    private var selectedGameUri: Uri? = null
    private val storageRoot: File by lazy {
        getExternalFilesDir("game-data") ?: File(filesDir, "game-data")
    }
    private val gameDirectory: File by lazy { File(storageRoot, "game") }
    private val stagingDirectory: File by lazy { File(storageRoot, "game-installing") }

    private val selectGameDocument = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        val uri = result.data?.data
        if (result.resultCode != RESULT_OK || uri == null) {
            selectedGameUri = null
            gameImageState = GameImageUiState()
            return@registerForActivityResult
        }
        validateSelectedImage(uri, result.data?.flags ?: 0)
    }

    private val createDefaultXexDocument = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        val uri = result.data?.data
        if (result.resultCode == RESULT_OK && uri != null) exportDefaultXex(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        System.loadLibrary("main")
        setContent {
            Project8Launcher(
                openDiagnostics = { startActivity(Intent(this, ProbeActivity::class.java)) },
                selectGame = ::selectGame,
                installGame = ::installGame,
                playGame = ::playGame,
                removeGame = ::removeGame,
                exportDefaultXex = ::requestDefaultXexExport,
                gameImageState = gameImageState
            )
        }
        // game-installing is never a valid title. Recover interrupted setup
        // before looking for game/, so PLAY cannot race a partial extraction.
        Thread {
            deleteRecursively(stagingDirectory)
            checkInstalledGame()
        }.start()
    }

    private fun selectGame() {
        gameImageState = GameImageUiState(GameImagePhase.Selecting)
        selectGameDocument.launch(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        })
    }

    private fun validateSelectedImage(uri: Uri, grantedFlags: Int) {
        selectedGameUri = uri
        val readFlags = grantedFlags and Intent.FLAG_GRANT_READ_URI_PERMISSION
        if (readFlags != 0) {
            try {
                contentResolver.takePersistableUriPermission(uri, readFlags)
                getSharedPreferences("game-image", MODE_PRIVATE)
                    .edit()
                    .putString("selected-uri", uri.toString())
                    .apply()
            } catch (_: SecurityException) {
                // The URI remains valid for this selection even if persistence is unavailable.
            }
        }
        gameImageState = GameImageUiState(GameImagePhase.Validating)
        Thread {
            val validation = try {
                val descriptor = contentResolver.openFileDescriptor(uri, "r")
                if (descriptor == null) {
                    GameImageValidation(1, 0, 0, false, "", "The selected file could not be opened.")
                } else {
                    nativeValidateGameImage(descriptor.detachFd())
                }
            } catch (_: Exception) {
                GameImageValidation(1, 0, 0, false, "", "The selected file could not be opened.")
            }
            runOnUiThread {
                gameImageState = GameImageUiState(
                    if (validation.state in 0..4 && validation.state != 1) {
                        GameImagePhase.Detected
                    } else {
                        GameImagePhase.Error
                    },
                    validation
                )
            }
        }.start()
    }

    private fun playGame() {
        if (gameImageState.phase != GameImagePhase.Installed) return
        startActivity(Intent(this, GameplayActivity::class.java)
            .putExtra(GameplayActivity.EXTRA_GAME_ROOT, gameDirectory.absolutePath))
    }

    private fun checkInstalledGame() {
        if (!gameDirectory.isDirectory) return
        val result = nativeValidateInstalledGame(gameDirectory.absolutePath)
        if (!result.success) deleteRecursively(gameDirectory)
        runOnUiThread {
            gameImageState = if (result.success) {
                GameImageUiState(GameImagePhase.Installed, install = result)
            } else {
                GameImageUiState()
            }
        }
    }

    private fun installGame() {
        val uri = selectedGameUri ?: return
        if (gameDirectory.exists()) return
        gameImageState = gameImageState.copy(phase = GameImagePhase.Installing,
            install = GameInstallResult(false, 0, 0, "Preparing installation..."))
        Thread {
            val result = try {
                if (!deleteRecursively(stagingDirectory)) {
                    throw IllegalStateException("The incomplete installation could not be cleared.")
                }
                if (!stagingDirectory.mkdirs()) throw IllegalStateException("The installation folder could not be created.")
                val descriptor = contentResolver.openFileDescriptor(uri, "r")
                    ?: throw IllegalStateException("Android could not open the selected game image.")
                nativeInstallGameImage(descriptor.detachFd(), stagingDirectory.absolutePath)
            } catch (exception: Exception) {
                GameInstallResult(false, 0, 0, exception.message ?: "Installation stopped.")
            }
            if (result.success) {
                try {
                    finalizeInstallation()
                } catch (exception: Exception) {
                    deleteRecursively(stagingDirectory)
                    runOnUiThread { gameImageState = gameImageState.copy(
                        phase = GameImagePhase.Error, install = GameInstallResult(false, 0, 0,
                            exception.message ?: "Installation could not be finalized.")) }
                    return@Thread
                }
            } else {
                deleteRecursively(stagingDirectory)
            }
            runOnUiThread {
                gameImageState = if (result.success) GameImageUiState(GameImagePhase.Installed, install = result)
                else gameImageState.copy(phase = GameImagePhase.Error, install = result)
            }
        }.start()
    }

    // Staging and final directories share the app-specific storage parent. A
    // valid game is never replaced; remove is explicit, then rename is atomic
    // where the filesystem supports it.
    private fun finalizeInstallation() {
        if (!nativeValidateInstalledGame(stagingDirectory.absolutePath).success) {
            throw IllegalStateException("The extracted game did not pass validation.")
        }
        if (gameDirectory.exists()) throw IllegalStateException("An installed game already exists.")
        try {
            Files.move(stagingDirectory.toPath(), gameDirectory.toPath(), StandardCopyOption.ATOMIC_MOVE)
        } catch (_: AtomicMoveNotSupportedException) {
            Files.move(stagingDirectory.toPath(), gameDirectory.toPath())
        }
    }

    private fun removeGame() {
        if (gameImageState.phase != GameImagePhase.Installed) return
        gameImageState = GameImageUiState(GameImagePhase.Removing)
        Thread {
            val removed = deleteRecursively(gameDirectory)
            runOnUiThread {
                gameImageState = if (removed) GameImageUiState()
                else GameImageUiState(GameImagePhase.Error, install = GameInstallResult(false, 0, 0,
                    "The installed game could not be removed."))
            }
        }.start()
    }

    /** Called by native extraction on the worker thread; UI work stays queued. */
    fun onNativeInstallProgress(copied: Long, total: Long, currentFile: String) {
        runOnUiThread {
            if (gameImageState.phase == GameImagePhase.Installing) {
                gameImageState = gameImageState.copy(install = GameInstallResult(false, copied, total,
                    "Installing $currentFile"))
            }
        }
    }

    private fun deleteRecursively(file: File): Boolean {
        if (!file.exists()) return true
        val childrenRemoved = file.listFiles()?.all(::deleteRecursively) ?: true
        return childrenRemoved && (file.delete() || !file.exists())
    }

    private fun requestDefaultXexExport() {
        val validation = gameImageState.validation
        if (!BuildConfig.DEBUG || selectedGameUri == null || validation?.state != 0 ||
            validation.build != "Supported") return
        createDefaultXexDocument.launch(Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/octet-stream"
            putExtra(Intent.EXTRA_TITLE, "default.xex")
        })
    }

    private fun exportDefaultXex(destination: Uri) {
        val source = selectedGameUri ?: return
        gameImageState = gameImageState.copy(GameImagePhase.Exporting, exportMessage = null)
        Thread {
            val result = try {
                val input = contentResolver.openFileDescriptor(source, "r")
                val output = contentResolver.openFileDescriptor(destination, "w")
                if (input == null || output == null) {
                    input?.close()
                    output?.close()
                    XexExportResult(false, "The destination file could not be opened.")
                } else {
                    nativeExportDefaultXex(input.detachFd(), output.detachFd())
                }
            } catch (_: Exception) {
                XexExportResult(false, "The executable could not be exported.")
            }
            runOnUiThread {
                gameImageState = gameImageState.copy(
                    phase = GameImagePhase.Detected,
                    exportMessage = result.message
                )
            }
        }.start()
    }

    private external fun nativeValidateGameImage(detachedFd: Int): GameImageValidation
    private external fun nativeInstallGameImage(detachedFd: Int, stagingPath: String): GameInstallResult
    private external fun nativeValidateInstalledGame(gamePath: String): GameInstallResult
    private external fun nativeExportDefaultXex(inputFd: Int, outputFd: Int): XexExportResult
}

data class GameImageValidation(
    val state: Int,
    val sizeBytes: Long,
    val rootSector: Long,
    val defaultXexFound: Boolean,
    val build: String,
    val message: String
)

data class XexExportResult(val success: Boolean, val message: String)

private enum class GameImagePhase { Idle, Selecting, Validating, Exporting, Detected, Installing, Installed, Removing, Error }

private data class GameImageUiState(
    val phase: GameImagePhase = GameImagePhase.Idle,
    val validation: GameImageValidation? = null,
    val install: GameInstallResult? = null,
    val exportMessage: String? = null
)

data class GameInstallResult(val success: Boolean, val copiedBytes: Long, val totalBytes: Long,
                             val message: String)

private enum class LauncherPage(val label: String) {
    Home("HOME"), Game("GAME"), Graphics("GRAPHICS"), Settings("SETTINGS"), About("ABOUT")
}

private val NearBlack = Color(0xFF090A0A)
private val PanelBlack = Color(0xFF101212)
private val Primary = Color(0xFFF4F5F3)
private val Secondary = Color(0xFF9BA09C)
private val Accent = Color(0xFF76B82A)

@Composable
private fun Project8Launcher(
    openDiagnostics: () -> Unit,
    selectGame: () -> Unit,
    installGame: () -> Unit,
    playGame: () -> Unit,
    removeGame: () -> Unit,
    exportDefaultXex: () -> Unit,
    gameImageState: GameImageUiState
) {
    var page by remember { mutableStateOf(LauncherPage.Home) }
    BoxWithConstraints(
        modifier = Modifier
            .fillMaxSize()
            .background(NearBlack)
            .windowInsetsPadding(WindowInsets.safeDrawing)
    ) {
        val compact = maxHeight < 360.dp
        val horizontalPadding = if (maxWidth < 640.dp) 20.dp else 36.dp
        val verticalPadding = if (compact) 12.dp else 24.dp

        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = horizontalPadding, vertical = verticalPadding)
        ) {
            TopNavigation(page, { page = it }, compact)
            Spacer(Modifier.height(if (compact) 14.dp else 28.dp))
            Box(Modifier.fillMaxWidth().weight(1f), contentAlignment = Alignment.Center) {
                when (page) {
                    LauncherPage.Home -> HomePage(compact, gameImageState, playGame)
                    LauncherPage.Game -> GamePage(compact, gameImageState, selectGame, installGame, removeGame, exportDefaultXex)
                    LauncherPage.Graphics -> PlaceholderPage(
                        "GRAPHICS",
                        "Graphics and performance options will be available as the game runtime integration advances.",
                        compact
                    )
                    LauncherPage.Settings -> PlaceholderPage(
                        "SETTINGS",
                        "Application and driver configuration will be available in a future update.",
                        compact
                    )
                    LauncherPage.About -> AboutPage(openDiagnostics, compact)
                }
            }
        }
    }
}

@Composable
private fun TopNavigation(selected: LauncherPage, select: (LauncherPage) -> Unit, compact: Boolean) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(if (compact) 18.dp else 28.dp)
    ) {
        LauncherPage.entries.forEach { page ->
            Column(
                modifier = Modifier.clickable { select(page) },
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    page.label,
                    color = if (page == selected) Primary else Secondary,
                    fontWeight = if (page == selected) FontWeight.SemiBold else FontWeight.Medium,
                    fontSize = if (compact) 12.sp else 13.sp,
                    letterSpacing = 1.1.sp,
                    maxLines = 1
                )
                Spacer(Modifier.height(7.dp))
                Box(
                    Modifier
                        .height(2.dp)
                        .width(if (page == selected) 24.dp else 10.dp)
                        .background(if (page == selected) Accent else Color.Transparent)
                )
            }
        }
    }
}

@Composable
private fun HomePage(compact: Boolean, state: GameImageUiState, playGame: () -> Unit) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Box(
            modifier = Modifier
                .size(if (compact) 54.dp else 78.dp)
                .background(PanelBlack)
                .alpha(0.8f)
        )
        Spacer(Modifier.height(if (compact) 14.dp else 24.dp))
        Text(
            "PROJECT 8",
            color = Primary,
            fontSize = if (compact) 30.sp else 42.sp,
            fontWeight = FontWeight.Light,
            letterSpacing = 4.sp
        )
        if (state.phase == GameImagePhase.Installed) {
            Spacer(Modifier.height(if (compact) 18.dp else 28.dp))
            Action("PLAY", playGame)
        }
        Spacer(Modifier.height(10.dp))
        Text(
            "Android ARM64 Recompilation Project",
            color = Secondary,
            fontSize = if (compact) 13.sp else 15.sp,
            textAlign = TextAlign.Center
        )
    }
}

@Composable
private fun GamePage(
    compact: Boolean,
    state: GameImageUiState,
    selectGame: () -> Unit,
    installGame: () -> Unit,
    removeGame: () -> Unit,
    exportDefaultXex: () -> Unit
) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        PageTitle("GAME", compact)
        Spacer(Modifier.height(if (compact) 18.dp else 30.dp))
        Text(
            when (state.phase) {
                GameImagePhase.Selecting -> "Selecting..."
                GameImagePhase.Validating -> "Validating..."
                GameImagePhase.Exporting -> "Exporting..."
                GameImagePhase.Detected -> "Game image ready"
                GameImagePhase.Installing -> "Installing..."
                GameImagePhase.Installed -> "TONY HAWK'S PROJECT 8\nINSTALLED"
                GameImagePhase.Removing -> "Removing..."
                else -> "Select your game"
            },
            color = Primary,
            fontSize = if (compact) 19.sp else 22.sp
        )
        Spacer(Modifier.height(8.dp))
        Text(
            state.install?.message ?: state.validation?.message
                ?: "Select your legally owned game image. It stays outside the app.",
            color = Secondary,
            fontSize = 14.sp,
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(horizontal = 24.dp)
        )
        if (state.phase == GameImagePhase.Detected && state.validation != null) {
            Spacer(Modifier.height(8.dp))
            Text(
                if (state.validation.defaultXexFound) "Executable identified" else "Executable not found",
                color = if (state.validation.defaultXexFound) Accent else Secondary,
                fontSize = 12.sp,
                letterSpacing = 0.5.sp
            )
            if (state.validation.defaultXexFound) {
                Spacer(Modifier.height(4.dp))
                Text("Build: ${state.validation.build}", color = Secondary, fontSize = 12.sp)
            }
        }
        if (state.phase == GameImagePhase.Installing && (state.install?.totalBytes ?: 0) > 0) {
            Spacer(Modifier.height(8.dp))
            Text("${state.install!!.copiedBytes * 100 / state.install.totalBytes}%", color = Accent, fontSize = 14.sp)
        }
        if (state.exportMessage != null) {
            Spacer(Modifier.height(8.dp))
            Text(state.exportMessage, color = Secondary, fontSize = 12.sp, textAlign = TextAlign.Center)
        }
        Spacer(Modifier.height(if (compact) 18.dp else 28.dp))
        when (state.phase) {
            GameImagePhase.Installed -> Action("REMOVE GAME", removeGame)
            GameImagePhase.Detected -> {
                Action("INSTALL GAME", installGame)
                Spacer(Modifier.height(10.dp))
                Action("SELECT ANOTHER ISO", selectGame)
            }
            GameImagePhase.Installing, GameImagePhase.Removing -> Unit
            else -> Action("SELECT ISO", selectGame)
        }
        if (BuildConfig.DEBUG && state.validation?.state == 0 && state.validation.build == "Supported") {
            Spacer(Modifier.height(10.dp))
            Action("EXPORT DEFAULT.XEX", exportDefaultXex)
        }
    }
}

@Composable
private fun PlaceholderPage(title: String, message: String, compact: Boolean) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        PageTitle(title, compact)
        Spacer(Modifier.height(if (compact) 20.dp else 34.dp))
        Text("COMING SOON", color = Accent, fontSize = 12.sp, letterSpacing = 1.3.sp)
        Spacer(Modifier.height(12.dp))
        Text(
            message,
            color = Secondary,
            fontSize = 15.sp,
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(horizontal = 28.dp)
        )
    }
}

@Composable
private fun AboutPage(openDiagnostics: () -> Unit, compact: Boolean) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        PageTitle("ABOUT", compact)
        Spacer(Modifier.height(if (compact) 16.dp else 26.dp))
        Text("Project 8 Android", color = Primary, fontSize = 21.sp, fontWeight = FontWeight.Medium)
        Spacer(Modifier.height(7.dp))
        Text("Experimental Android ARM64 recompilation project.", color = Secondary, fontSize = 14.sp)
        Spacer(Modifier.height(if (compact) 16.dp else 24.dp))
        InfoLine("Runtime", "ReXGlue")
        InfoLine("Platform", "Android ARM64")
        Spacer(Modifier.height(if (compact) 18.dp else 26.dp))
        Action("DIAGNOSTICS", openDiagnostics)
    }
}

@Composable
private fun PageTitle(title: String, compact: Boolean) {
    Text(
        title,
        color = Primary,
        fontSize = if (compact) 21.sp else 26.sp,
        fontWeight = FontWeight.SemiBold,
        letterSpacing = 1.4.sp
    )
}

@Composable
private fun InfoLine(label: String, value: String) {
    Row(modifier = Modifier.fillMaxWidth(0.62f), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, color = Secondary, fontSize = 14.sp)
        Text(value, color = Primary, fontSize = 14.sp, fontWeight = FontWeight.Medium)
    }
    Spacer(Modifier.height(8.dp))
}

@Composable
private fun Action(label: String, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .background(PanelBlack)
            .clickable(onClick = onClick)
            .padding(horizontal = 22.dp, vertical = 11.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(label, color = Accent, fontSize = 12.sp, letterSpacing = 1.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}
