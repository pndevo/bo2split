<#
.SYNOPSIS
  Small window for the mod's settings: the input device of each player (keyboard and mouse or
  one of the XInput pads, chosen from a list or by pressing a button on the device), the
  player count, the pad button layout, look and aim, screen and start settings, and the end
  of bo2ss.log.
.DESCRIPTION
  The values shown and written are those of bo2ss.ini in the game folder; most keys are read
  by the DLL every frame, so a change shows at once. A key the game-folder ini does not have
  yet is appended on its first change. The descriptions (and the defaults for a key the game
  ini lacks) come from the bo2ss.ini next to this script, else from ..\public\bo2ss.ini (the
  shipped file), else from ..\build\bo2ss.ini, else from the game-folder ini itself.
  The game folder is found in this order: the folder of this script (when t6zm.exe is there),
  -GameDir, the Steam library entry of the game.
  Start it with panel.bat (double click) or:
  powershell -NoProfile -ExecutionPolicy Bypass -STA -File panel.ps1
  -Tab pads opens it on the "Players and pads" tab.
  When panel-dev.ps1 lies next to this script it is loaded and adds the development tabs and
  buttons to the same window.
#>
param([string]$Tab = '', [string]$GameDir = '')
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase, System.Data, System.Xaml
if (-not ('Bo2Pad' -as [type])) {
Add-Type @"
using System; using System.Runtime.InteropServices;
public class Bo2Pad {
  [StructLayout(LayoutKind.Sequential)] public struct State { public uint packet; public ushort buttons; public byte lt, rt; public short lx, ly, rx, ry; }
  [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")] static extern uint Get(uint index, out State s);
  public static bool Connected(int index) { State s; try { return Get((uint)index, out s) == 0; } catch { return false; } }
  public static ushort Buttons(int index) { State s; try { return Get((uint)index, out s) == 0 ? s.buttons : (ushort)0; } catch { return 0; } }
  // Buttons plus two extra bits for the triggers (0x10000 = LT, 0x20000 = RT).
  public static int Held(int index) { State s; try { if (Get((uint)index, out s) != 0) return 0; return s.buttons | (s.lt > 60 ? 0x10000 : 0) | (s.rt > 60 ? 0x20000 : 0); } catch { return 0; } }
}
"@
}

# ---- game folder -------------------------------------------------------------------------
function Test-GameDir([string]$dir) {
    if (-not $dir) { return $false }
    try { return [System.IO.File]::Exists([System.IO.Path]::Combine($dir, 't6zm.exe')) } catch { return $false }
}
function Get-SteamLibraries {
    # Steam install folders from the registry plus the library folders listed in libraryfolders.vdf.
    $roots = @()
    try { $v = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction Stop).SteamPath; if ($v) { $roots += $v } } catch { }
    try { $v = (Get-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -Name InstallPath -ErrorAction Stop).InstallPath; if ($v) { $roots += $v } } catch { }
    $libs = New-Object System.Collections.ArrayList
    foreach ($root in $roots) {
        $root = $root.Replace('/', '\')
        if ($libs -notcontains $root) { [void]$libs.Add($root) }
        try {
            $vdf = [System.IO.Path]::Combine($root, 'steamapps\libraryfolders.vdf')
            if ([System.IO.File]::Exists($vdf)) {
                foreach ($line in [System.IO.File]::ReadAllLines($vdf)) {
                    if ($line -match '^\s*"path"\s+"([^"]+)"') {
                        $p = $Matches[1].Replace('\\', '\')
                        if ($libs -notcontains $p) { [void]$libs.Add($p) }
                    }
                }
            }
        } catch { }
    }
    return $libs
}
function Find-GameDir([string]$Hint = '', [string]$ScriptDir = '') {
    # 1. the folder of the script, 2. the given path, 3. the Steam libraries. $null when nothing is found.
    if (Test-GameDir $ScriptDir) { return $ScriptDir }
    if (Test-GameDir $Hint) { return $Hint }
    foreach ($lib in (Get-SteamLibraries)) {
        $dir = ''
        try { $dir = [System.IO.Path]::Combine($lib, 'steamapps\common\Call of Duty Black Ops II') } catch { }
        if (Test-GameDir $dir) { return $dir }
    }
    return $null
}
function Test-File([string]$path) { return [bool]($path -and (Test-Path -LiteralPath $path -PathType Leaf)) }

$script:GameDir = Find-GameDir -Hint $GameDir -ScriptDir $PSScriptRoot
$script:GameIni = if ($script:GameDir) { Join-Path $script:GameDir 'bo2ss.ini' } else { '' }
$script:LogPath = if ($script:GameDir) { Join-Path $script:GameDir 'bo2ss.log' } else { '' }
$script:DescIni = ''
foreach ($c in (Join-Path $PSScriptRoot 'bo2ss.ini'), (Join-Path $PSScriptRoot '..\public\bo2ss.ini'), (Join-Path $PSScriptRoot '..\build\bo2ss.ini'), $script:GameIni) {
    if (Test-File $c) { $script:DescIni = $c; break }
}
$script:Loading = $false
$script:IniStamp = [datetime]::MinValue

# ---- ini model -------------------------------------------------------------------------
# The keys the controls of this panel name. Anything else (the development part) adds its own rows.
$script:PlayerKeys = @('p1_pad', 'p2_pad', 'p3_pad', 'p4_pad', 'extra_players', 'split_vertical', 'stance_hold_ms',
    'btn_rt', 'btn_lt', 'btn_rb', 'btn_lb', 'btn_a', 'btn_b', 'btn_x', 'btn_y', 'btn_ls', 'btn_rs',
    'btn_dpad_up', 'btn_dpad_down', 'btn_dpad_left', 'btn_dpad_right', 'btn_back', 'btn_start',
    'player_rumble', 'look_native', 'look_accel', 'look_speed', 'look_deadzone', 'look_curve', 'look_ads_scale', 'mouse_sens',
    'fov_scale', 'layout_three_players', 'map', 'startlocation', 'gametype', 'auto_start_after_ms', 'lobby_join')
function Read-Descriptions([string]$path) {
    # Key, default and description (the comment lines above the key; keys that follow each
    # other without a comment in between share it).
    $rows = New-Object System.Collections.ArrayList
    if (-not (Test-File $path)) { return $rows }
    $comment = New-Object System.Collections.ArrayList
    $afterKey = $false
    foreach ($line in [System.IO.File]::ReadAllLines($path)) {
        if ($line -match '^\s*;\s?(.*)$') {
            if ($afterKey) { $comment.Clear(); $afterKey = $false }
            [void]$comment.Add($Matches[1].Trim())
        } elseif ($line -match '^([A-Za-z0-9_]+)=(.*)$') {
            [void]$rows.Add([pscustomobject]@{ Key = $Matches[1]; Default = $Matches[2]; Info = ($comment -join ' ') })
            $afterKey = $true
        } elseif ($line.Trim() -eq '') { $comment.Clear(); $afterKey = $false }
    }
    return $rows
}
function Read-GameIni {
    $values = @{}
    if (Test-File $script:GameIni) {
        foreach ($line in [System.IO.File]::ReadAllLines($script:GameIni)) {
            if ($line -match '^([A-Za-z0-9_]+)=(.*)$') { $values[$Matches[1]] = $Matches[2] }
        }
    }
    return $values
}
function Write-GameIni([string]$key, [string]$value) {
    # Temp file plus move (the game reads the ini every frame).
    $txt = [System.IO.File]::ReadAllText($script:GameIni)
    $pattern = '(?m)^' + [regex]::Escape($key) + '=.*$'
    if ($txt -match $pattern) { $txt = [regex]::Replace($txt, $pattern, ("$key=$value").Replace('$', '$$')) }
    else { $txt = $txt.TrimEnd("`r", "`n") + "`r`n$key=$value`r`n" }
    $tmp = $script:GameIni + '.tmp'
    [System.IO.File]::WriteAllText($tmp, $txt, [System.Text.Encoding]::ASCII)
    Move-Item -Path $tmp -Destination $script:GameIni -Force
    $script:IniStamp = (Get-Item $script:GameIni).LastWriteTimeUtc
}

$script:DescRows = Read-Descriptions $script:DescIni
$script:Desc = @{}
foreach ($r in $script:DescRows) { $script:Desc[$r.Key] = $r }
$script:Table = New-Object System.Data.DataTable
foreach ($c in 'Mark', 'Key', 'Value', 'Default', 'Grp', 'Info') { [void]$script:Table.Columns.Add($c, [string]) }
function Add-TableRow([string]$key, [string]$default, [string]$info) {
    $row = $script:Table.NewRow()
    $row.Key = $key; $row.Default = $default; $row.Info = $info; $row.Grp = ''
    [void]$script:Table.Rows.Add($row)
}
function Import-Values {
    # Fills / refreshes the table from the game-folder ini without writing anything back. A
    # player key gets a row when the description ini or the game-folder ini knows it.
    $script:Loading = $true
    try {
        $game = Read-GameIni
        if ($script:Table.Rows.Count -eq 0) {
            foreach ($k in $script:PlayerKeys) {
                if ($script:Desc.ContainsKey($k)) { Add-TableRow $k $script:Desc[$k].Default $script:Desc[$k].Info }
                elseif ($game.ContainsKey($k)) { Add-TableRow $k $game[$k] '' }
            }
        }
        foreach ($row in $script:Table.Rows) {
            $has = $game.ContainsKey($row.Key)
            $v = if ($has) { $game[$row.Key] } else { $row.Default }
            if ($row.Value -ne $v) { $row.Value = $v }
            $mark = if (-not $has) { 'new' } elseif ($v -ne $row.Default) { '*' } else { '' }
            if ($row.Mark -ne $mark) { $row.Mark = $mark }
        }
        if (Test-File $script:GameIni) { $script:IniStamp = (Get-Item $script:GameIni).LastWriteTimeUtc }
    } finally { $script:Loading = $false }
}
function Set-Key([string]$key, [string]$value) {
    # Goes through the table, so every view of the value stays the same.
    foreach ($row in $script:Table.Rows) { if ($row.Key -eq $key) { if ($row.Value -ne $value) { $row.Value = $value }; return } }
}
$script:Table.add_ColumnChanged({
    param($s, $e)
    if ($script:Loading -or $e.Column.ColumnName -ne 'Value') { return }
    $row = $e.Row
    $value = ([string]$row.Value).Trim()
    try {
        Write-GameIni $row.Key $value
        $script:Loading = $true
        $row.Mark = if ($value -ne $row.Default) { '*' } else { '' }
        $script:Loading = $false
        $script:Status.Text = "written: $($row.Key)=$value"
    } catch { $script:Loading = $false; $script:Status.Text = "write failed: $($_.Exception.Message)" }
})

# ---- window ----------------------------------------------------------------------------
[xml]$xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="bo2-splitscreen panel" Width="1180" Height="800" MinWidth="760" MinHeight="480" FontSize="13">
  <DockPanel>
    <Border DockPanel.Dock="Top" Padding="8" Background="#f0f0f3" BorderBrush="#c8c8d0" BorderThickness="0,0,0,1">
      <DockPanel>
        <StackPanel x:Name="ToolButtons" Orientation="Horizontal" DockPanel.Dock="Left">
          <Button x:Name="BtnOpenIni" Content="Open ini" Padding="10,4" Margin="0,0,6,0"/>
          <Button x:Name="BtnOpenLog" Content="Open log" Padding="10,4" Margin="0,0,14,0"/>
        </StackPanel>
        <TextBlock x:Name="GameState" VerticalAlignment="Center" FontWeight="SemiBold" DockPanel.Dock="Left" Margin="0,0,14,0"/>
        <TextBlock x:Name="Status" VerticalAlignment="Center" Foreground="#555" TextTrimming="CharacterEllipsis"/>
      </DockPanel>
    </Border>
    <Border x:Name="Banner" DockPanel.Dock="Top" Padding="12" Background="#fff4ce" BorderBrush="#d4a72c" BorderThickness="0,0,0,1" Visibility="Collapsed">
      <TextBlock x:Name="BannerText" TextWrapping="Wrap" FontSize="14"/>
    </Border>
    <Grid DockPanel.Dock="Bottom" Height="190">
      <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/></Grid.RowDefinitions>
      <StackPanel Orientation="Horizontal" Margin="8,4">
        <TextBlock Text="bo2ss.log" FontWeight="SemiBold" VerticalAlignment="Center" Margin="0,0,12,0"/>
        <CheckBox x:Name="LogLive" Content="follow" IsChecked="True" VerticalAlignment="Center" Margin="0,0,12,0"/>
        <CheckBox x:Name="LogQuiet" Content="hide [view] lines" IsChecked="True" VerticalAlignment="Center" Margin="0,0,12,0"/>
        <TextBlock Text="filter" VerticalAlignment="Center" Margin="0,0,4,0"/>
        <TextBox x:Name="LogFilter" Width="220" VerticalAlignment="Center"/>
      </StackPanel>
      <TextBox x:Name="LogBox" Grid.Row="1" IsReadOnly="True" FontFamily="Consolas" FontSize="12" TextWrapping="NoWrap"
               VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Auto" BorderThickness="0,1,0,0"/>
    </Grid>
    <TabControl x:Name="Tabs" Margin="0">
      <TabItem Header="Game">
        <ScrollViewer VerticalScrollBarVisibility="Auto"><StackPanel x:Name="QuickPanel" Margin="14"/></ScrollViewer>
      </TabItem>
      <TabItem Header="Players and pads">
        <ScrollViewer VerticalScrollBarVisibility="Auto"><StackPanel x:Name="PadPanel" Margin="14"/></ScrollViewer>
      </TabItem>
    </TabControl>
  </DockPanel>
</Window>
'@
$win = [Windows.Markup.XamlReader]::Load((New-Object System.Xml.XmlNodeReader $xaml))
foreach ($n in 'ToolButtons', 'BtnOpenIni', 'BtnOpenLog', 'GameState', 'Status', 'Banner', 'BannerText', 'LogLive', 'LogQuiet', 'LogFilter', 'LogBox', 'PadPanel', 'QuickPanel', 'Tabs') {
    Set-Variable -Scope Script -Name $n -Value $win.FindName($n)
}
# Hooks for panel-dev.ps1: a grid that may hold the keyboard focus (no reload while editing in
# it) and a block that runs after the ini was reloaded from outside.
$script:Grid = $null
$script:AfterReload = $null

Import-Values

# ---- players and pads --------------------------------------------------------------------
$script:Actions = @('0  none', '1  fire', '2  aim', '3  lethal grenade', '4  tactical grenade', '5  use / reload', '6  jump', '7  crouch (tap) / prone (hold)',
                    '8  sprint', '9  melee', '10  next weapon', '11  action slot 1', '12  action slot 2', '13  action slot 3', '14  action slot 4',
                    '15  scoreboard (while held)', '16  controller options', '17  previous weapon')
$script:PadInputs = @(
    @{ Key = 'btn_rt'; Name = 'RT'; Bit = 0x20000 }, @{ Key = 'btn_lt'; Name = 'LT'; Bit = 0x10000 },
    @{ Key = 'btn_rb'; Name = 'RB'; Bit = 0x0200 }, @{ Key = 'btn_lb'; Name = 'LB'; Bit = 0x0100 },
    @{ Key = 'btn_a'; Name = 'A'; Bit = 0x1000 }, @{ Key = 'btn_b'; Name = 'B'; Bit = 0x2000 },
    @{ Key = 'btn_x'; Name = 'X'; Bit = 0x4000 }, @{ Key = 'btn_y'; Name = 'Y'; Bit = 0x8000 },
    @{ Key = 'btn_ls'; Name = 'Left stick click'; Bit = 0x0040 }, @{ Key = 'btn_rs'; Name = 'Right stick click'; Bit = 0x0080 },
    @{ Key = 'btn_dpad_up'; Name = 'D-pad up'; Bit = 0x0001 }, @{ Key = 'btn_dpad_down'; Name = 'D-pad down'; Bit = 0x0002 },
    @{ Key = 'btn_dpad_left'; Name = 'D-pad left'; Bit = 0x0004 }, @{ Key = 'btn_dpad_right'; Name = 'D-pad right'; Bit = 0x0008 },
    @{ Key = 'btn_back'; Name = 'Back'; Bit = 0x0020 }, @{ Key = 'btn_start'; Name = 'Start'; Bit = 0x0010 })
function Get-Value([string]$key) { foreach ($row in $script:Table.Rows) { if ($row.Key -eq $key) { return [string]$row.Value } }; return $null }
function New-Text([string]$text, [double]$size = 13, [string]$weight = 'Normal', [string]$margin = '0,0,0,6') {
    $t = New-Object System.Windows.Controls.TextBlock
    $t.Text = $text; $t.FontSize = $size; $t.FontWeight = $weight; $t.Margin = $margin; $t.TextWrapping = 'Wrap'
    return $t
}
function New-Row([string]$label, $control, [double]$labelWidth = 150) {
    $p = New-Object System.Windows.Controls.StackPanel
    $p.Orientation = 'Horizontal'; $p.Margin = '0,0,0,5'
    $l = New-Text $label 13 'Normal' '0'
    $l.Width = $labelWidth; $l.VerticalAlignment = 'Center'
    [void]$p.Children.Add($l); [void]$p.Children.Add($control)
    return $p
}
[void]$script:PadPanel.Children.Add((New-Text 'Players' 16 'SemiBold'))
[void]$script:PadPanel.Children.Add((New-Text 'Choose how many players join and which device each one plays with. Connect the pads first (up to four XInput pads); a pad that is plugged in later shows up here within a second. Devices change at once, also during a match; the player count counts from the next match start.' 13 'Normal' '0,0,0,10'))
$script:ExtraCombo = New-Object System.Windows.Controls.ComboBox
$script:ExtraCombo.Width = 230
foreach ($t in '2 players', '3 players', '4 players') { [void]$script:ExtraCombo.Items.Add($t) }
[void]$script:PadPanel.Children.Add((New-Row 'Players' $script:ExtraCombo 90))
$script:SplitCombo = New-Object System.Windows.Controls.ComboBox
$script:SplitCombo.Width = 230
foreach ($t in 'left / right', 'top / bottom') { [void]$script:SplitCombo.Items.Add($t) }
[void]$script:PadPanel.Children.Add((New-Row 'Split (2 players)' $script:SplitCombo 90))
[void]$script:PadPanel.Children.Add((New-Text 'Three or four players always split the screen in quarters.' 12 'Normal' '0,0,0,14'))

# One row per player: device list, "assign by pressing", state.
$script:DeviceValues = @(-3, -2, 0, 1, 2, 3, -1)
$script:DeviceKeys = @('p1_pad', 'p2_pad', 'p3_pad', 'p4_pad')
$script:DeviceCombos = @(); $script:AssignButtons = @(); $script:DeviceStates = @()
$script:Listen = -1; $script:ListenUntil = [datetime]::MinValue; $script:ListenArmed = $false
function Get-DeviceText([int]$v) {
    if ($v -eq -3) { return 'Automatic (player 1: first connected pad)' }
    if ($v -eq -2) { return 'Keyboard + mouse' }
    if ($v -eq -1) { return 'Nothing' }
    return "Pad $($v + 1)  ($(if ([Bo2Pad]::Connected($v)) { 'connected' } else { 'not connected' }))"
}
function Get-Device([int]$player) {
    $v = 0
    if (-not [int]::TryParse((Get-Value $script:DeviceKeys[$player]), [ref]$v)) { return -1 }
    if ($v -lt -3 -or $v -gt 3) { return -1 }
    return $v
}
function Set-Device([int]$player, [int]$device) {
    # A device has one player: whoever held it before gets this player's old device.
    $old = Get-Device $player
    if ($old -eq $device) { return }
    if ($device -ne -1) {
        foreach ($q in 0..3) { if ($q -ne $player -and (Get-Device $q) -eq $device) { Set-Key $script:DeviceKeys[$q] ([string]$old) } }
    }
    Set-Key $script:DeviceKeys[$player] ([string]$device)
    Sync-PadTab
}
function Stop-Listen([string]$message) {
    if ($script:Listen -ge 0) { $script:AssignButtons[$script:Listen].Content = 'Assign by pressing' }
    $script:Listen = -1
    if ($message) { $script:Status.Text = $message }
}
foreach ($player in 0..3) {
    $combo = New-Object System.Windows.Controls.ComboBox
    $combo.Width = 230; $combo.Tag = $player
    foreach ($v in $script:DeviceValues) { [void]$combo.Items.Add((Get-DeviceText $v)) }
    $combo.add_SelectionChanged({ param($s, $e) if (-not $script:Loading -and $s.SelectedIndex -ge 0) { Set-Device ([int]$s.Tag) $script:DeviceValues[$s.SelectedIndex] } })
    $button = New-Object System.Windows.Controls.Button
    $button.Content = 'Assign by pressing'; $button.Width = 190; $button.Margin = '8,0,8,0'; $button.Padding = '6,2'; $button.Tag = $player
    $button.add_Click({
        param($s, $e)
        $p = [int]$s.Tag
        if ($script:Listen -eq $p) { Stop-Listen 'assignment cancelled'; return }
        Stop-Listen ''
        $script:Listen = $p; $script:ListenUntil = (Get-Date).AddSeconds(15); $script:ListenArmed = $false
        $s.Content = 'press a pad button or a key ...'
        $script:Status.Text = "player $($p + 1): press a button on the pad, or a key for keyboard + mouse (Esc cancels)"
    })
    $state = New-Text '' 12 'Normal' '0'
    $state.VerticalAlignment = 'Center'
    $row = New-Row "Player $($player + 1)" $combo 90
    [void]$row.Children.Add($button); [void]$row.Children.Add($state)
    [void]$script:PadPanel.Children.Add($row)
    $script:DeviceCombos += $combo; $script:AssignButtons += $button; $script:DeviceStates += $state
}
[void]$script:PadPanel.Children.Add((New-Text 'Player 1 on a pad uses the game''s own gamepad bindings. Players 2 to 4 on a pad use the layout below. A player 2 to 4 on keyboard and mouse has a fixed layout: W A S D, mouse look, left / right button fire / aim, G lethal, Q tactical, F or R use / reload, space jump, C or left ctrl crouch (hold: prone), left shift sprint, V or E melee, 1 / 2 / middle button next weapon, 3 4 5 6 action slots; Esc stays with player 1 (pause menu).' 12 'Normal' '0,6,0,16'))
[void]$script:PadPanel.Children.Add((New-Text 'Pad buttons of players 2 to 4' 16 'SemiBold'))
[void]$script:PadPanel.Children.Add((New-Text 'One layout for every pad of players 2 to 4. A button lights up while it is held on one of their pads.' 12 'Normal' '0,0,0,10'))
$script:ActionCombos = @{}
$script:PadLabels = @{}
foreach ($in in $script:PadInputs) {
    if ($null -eq (Get-Value $in.Key)) { continue }
    $c = New-Object System.Windows.Controls.ComboBox
    $c.Width = 230; $c.Tag = $in.Key
    foreach ($a in $script:Actions) { [void]$c.Items.Add($a) }
    $c.add_SelectionChanged({ param($s, $e) if (-not $script:Loading -and $s.SelectedIndex -ge 0) { Set-Key ([string]$s.Tag) ([string]$s.SelectedIndex) } })
    $script:ActionCombos[$in.Key] = $c
    $row = New-Row $in.Name $c
    $script:PadLabels[$in.Key] = $row.Children[0]
    [void]$script:PadPanel.Children.Add($row)
}
$script:ExtraCombo.add_SelectionChanged({ if (-not $script:Loading -and $script:ExtraCombo.SelectedIndex -ge 0) { Set-Key 'extra_players' ([string]$script:ExtraCombo.SelectedIndex); Update-PadState } })
$script:SplitCombo.add_SelectionChanged({ if (-not $script:Loading -and $script:SplitCombo.SelectedIndex -ge 0) { Set-Key 'split_vertical' ($(if ($script:SplitCombo.SelectedIndex -eq 0) { '1' } else { '0' })) } })
function Sync-PadTab {
    # Shows the table's values in the pad tab without writing them back.
    $was = $script:Loading; $script:Loading = $true
    try {
        $v = 0
        foreach ($player in 0..3) { $script:DeviceCombos[$player].SelectedIndex = [array]::IndexOf($script:DeviceValues, (Get-Device $player)) }
        if ([int]::TryParse((Get-Value 'extra_players'), [ref]$v) -and $v -ge 0 -and $v -le 2) { $script:ExtraCombo.SelectedIndex = $v }
        $script:SplitCombo.SelectedIndex = $(if ((Get-Value 'split_vertical') -eq '0') { 1 } else { 0 })
        foreach ($k in $script:ActionCombos.Keys) {
            if ([int]::TryParse((Get-Value $k), [ref]$v) -and $v -ge 0 -and $v -lt $script:Actions.Count) { $script:ActionCombos[$k].SelectedIndex = $v }
        }
    } finally { $script:Loading = $was }
}
function Update-PadState {
    # Ten times a second: the state text per player, the held buttons, "assign by pressing".
    $held = @(0, 0, 0, 0); $connected = @($false, $false, $false, $false)
    foreach ($i in 0..3) { $connected[$i] = [Bo2Pad]::Connected($i); if ($connected[$i]) { $held[$i] = [Bo2Pad]::Held($i) } }
    if ($script:Listen -ge 0) {
        # Armed once no pad button is held, so the press that started it does not count.
        $any = -1
        foreach ($i in 0..3) { if ($held[$i]) { $any = $i; break } }
        if (-not $script:ListenArmed) { if ($any -lt 0) { $script:ListenArmed = $true } }
        elseif ($any -ge 0) { $p = $script:Listen; Stop-Listen "player $($p + 1) plays with pad $($any + 1)"; Set-Device $p $any }
        elseif ((Get-Date) -gt $script:ListenUntil) { Stop-Listen 'no button pressed, assignment cancelled' }
    }
    $players = 2 + [Math]::Max(0, $script:ExtraCombo.SelectedIndex)
    $layoutHeld = 0
    foreach ($player in 0..3) {
        $d = Get-Device $player
        $text = ''; $color = '#1a7f37'
        $twin = -1
        if ($d -ne -1) { foreach ($q in 0..3) { if ($q -lt $player -and (Get-Device $q) -eq $d) { $twin = $q; break } } }
        if ($player -ge $players) { $text = "not in the match (Players is $players)"; $color = '#777777' }
        elseif ($twin -ge 0) { $text = "same device as player $($twin + 1): no input"; $color = '#cf222e' }
        elseif ($d -eq -1) { $text = 'no input'; $color = '#9a6700' }
        elseif ($d -eq -2) { $text = 'keyboard + mouse' }
        elseif ($d -eq -3) {
            if ($player -eq 0) { $text = 'first connected pad, else keyboard + mouse' }
            else { $text = 'automatic is for player 1 only: no input'; $color = '#cf222e' }
        }
        elseif (-not $connected[$d]) { $text = "pad $($d + 1) is not connected"; $color = '#9a6700' }
        else {
            $names = @()
            foreach ($in in $script:PadInputs) { if ($held[$d] -band $in.Bit) { $names += $in.Name } }
            $text = if ($names.Count) { 'held: ' + ($names -join ', ') } else { 'ready' }
            if ($player -ge 1) { $layoutHeld = $layoutHeld -bor $held[$d] }
        }
        if ($script:DeviceStates[$player].Text -ne $text) { $script:DeviceStates[$player].Text = $text }
        $script:DeviceStates[$player].Foreground = $color
    }
    foreach ($in in $script:PadInputs) {
        $label = $script:PadLabels[$in.Key]
        if (-not $label) { continue }
        $on = ($layoutHeld -band $in.Bit) -ne 0
        $label.FontWeight = if ($on) { 'Bold' } else { 'Normal' }
        $label.Foreground = if ($on) { '#0969da' } else { '#000000' }
    }
}
function Update-DeviceLists {
    # Once a second: "connected" / "not connected" in the device lists.
    $was = $script:Loading; $script:Loading = $true
    try {
        foreach ($combo in $script:DeviceCombos) {
            $sel = $combo.SelectedIndex
            foreach ($i in 0..($script:DeviceValues.Count - 1)) {
                $t = Get-DeviceText $script:DeviceValues[$i]
                if ($combo.Items[$i] -ne $t) { $combo.Items[$i] = $t }
            }
            $combo.SelectedIndex = $sel
        }
    } finally { $script:Loading = $was }
}
$win.add_PreviewKeyDown({
    param($s, $e)
    if ($script:Listen -lt 0) { return }
    $e.Handled = $true
    if ($e.Key -eq 'Escape') { Stop-Listen 'assignment cancelled'; return }
    $p = $script:Listen
    Stop-Listen "player $($p + 1) plays with keyboard + mouse"
    Set-Device $p -2
})

# ---- check boxes and lists that write one key each ----------------------------------------
# Check boxes write 1 / 0, lists write the value in front of the entry's text. A control whose
# key is in neither ini is left out. All of them take effect while the game runs unless the text
# says otherwise. $panel is the StackPanel the control goes into.
$script:QuickChecks = @{}; $script:QuickCombos = @{}
function Add-QuickCheck([string]$key, [string]$label, [string]$hint, $panel = $script:QuickPanel) {
    if ($null -eq (Get-Value $key)) { return }
    $c = New-Object System.Windows.Controls.CheckBox
    $c.Content = $label; $c.Tag = $key; $c.Margin = '0,0,0,2'; $c.FontSize = 14
    $c.add_Click({ param($s, $e) if (-not $script:Loading) { Set-Key ([string]$s.Tag) ($(if ($s.IsChecked) { '1' } else { '0' })) } })
    $script:QuickChecks[$key] = $c
    [void]$panel.Children.Add($c)
    if ($hint) { [void]$panel.Children.Add((New-Text $hint 12 'Normal' '20,0,0,10')) }
}
function Add-QuickCombo([string]$key, [string]$label, [string[]]$entries, [string]$hint, $panel = $script:QuickPanel) {
    if ($null -eq (Get-Value $key)) { return }
    $c = New-Object System.Windows.Controls.ComboBox
    $c.Width = 330; $c.Tag = $key
    foreach ($t in $entries) { [void]$c.Items.Add($t) }
    $c.add_SelectionChanged({ param($s, $e) if (-not $script:Loading -and $s.SelectedIndex -ge 0) { Set-Key ([string]$s.Tag) (([string]$s.SelectedItem -split '\s+')[0]) } })
    $script:QuickCombos[$key] = $c
    [void]$panel.Children.Add((New-Row $label $c 170))
    if ($hint) { [void]$panel.Children.Add((New-Text $hint 12 'Normal' '170,0,0,10')) }
}
function Sync-QuickTab {
    # Shows the table's values in the check boxes and lists without writing them back. A value
    # the list does not have is added as "<value>  (from the ini)".
    $was = $script:Loading; $script:Loading = $true
    try {
        foreach ($k in $script:QuickChecks.Keys) { $on = (Get-Value $k) -ne '0'; if ($script:QuickChecks[$k].IsChecked -ne $on) { $script:QuickChecks[$k].IsChecked = $on } }
        foreach ($k in $script:QuickCombos.Keys) {
            $combo = $script:QuickCombos[$k]; $v = Get-Value $k; $index = -1
            foreach ($i in 0..($combo.Items.Count - 1)) { if ((([string]$combo.Items[$i]) -split '\s+')[0] -eq $v) { $index = $i; break } }
            if ($index -lt 0 -and $v -and -not $combo.IsDropDownOpen) { $index = $combo.Items.Add("$v  (from the ini)") }
            if ($combo.SelectedIndex -ne $index -and -not $combo.IsDropDownOpen) { $combo.SelectedIndex = $index }
        }
    } finally { $script:Loading = $was }
}

# Stance hold time, under the button layout.
Add-QuickCombo 'stance_hold_ms' 'Hold to go prone' @('150  ms, short', '300  ms, the game''s own time', '500  ms', '800  ms, long') 'How long the crouch button is held to go prone, or to stand up from prone.' $script:PadPanel
Sync-PadTab
Update-PadState

# ---- game tab: start, look and aim, screen ----------------------------------------------
[void]$script:QuickPanel.Children.Add((New-Text 'Start' 16 'SemiBold'))
Add-QuickCombo 'map' 'Map' @('zm_transit  TranZit', 'zm_tomb  Origins') 'Read when the lobby opens. A DLC map has to be owned and installed. Other maps are chosen in the lobby.'
Add-QuickCombo 'startlocation' 'Start location' @('transit  TranZit', 'town  Town', 'farm  Farm', 'diner  Diner', 'busdepot  Bus Depot', 'tomb  Origins') 'Has to fit the map. TranZit: transit for Classic, town, farm, diner or bus depot for Survival. Origins: tomb.'
Add-QuickCombo 'gametype' 'Game type' @('zclassic  Classic (Easter egg maps)', 'zstandard  Survival') 'Classic is the story mode of a map, Survival the plain round mode. TranZit and Origins: Classic on transit and tomb, Survival on town, farm, diner and busdepot.'
Add-QuickCombo 'auto_start_after_ms' 'Start of the match' @('0  lobby by hand: choose the map in the lobby and start there', '8000  starts by itself after the lobby opened') 'The lobby opens by itself either way.'
Add-QuickCheck 'lobby_join' 'Join in the lobby' 'A on a free pad joins the next player (2, 3, 4), B on a joined pad leaves. The joined pads then replace the player count and the pads set on "Players and pads".'
[void]$script:QuickPanel.Children.Add((New-Text 'Look and aim' 16 'SemiBold' '0,10,0,6'))
Add-QuickCombo 'look_native' 'Stick speed as the game' @('1  default, as player 1', '0  fixed speed below') 'Right stick of players 2 to 4 turns at the gamepad speeds of the game itself.'
Add-QuickCombo 'look_accel' 'Stick run-up' @('1  default, as the game', '0  full speed at once') 'The turn on a gamepad takes about a third of a second to reach full speed, for all players. 0 switches the run-up off.'
Add-QuickCombo 'look_speed' 'Stick speed' @('90  slow', '120', '180  default', '240', '300  fast') 'Turn rate of the right stick of players 2 to 4 (with the fixed speed).'
Add-QuickCombo 'look_deadzone' 'Dead zone' @('0  none', '10', '20  default', '30', '40') 'Percent of the right stick that does nothing.'
Add-QuickCombo 'look_curve' 'Curve' @('100  linear', '150  default, finer around the centre', '200  finest around the centre') 'How the stick travel turns into turn rate.'
Add-QuickCombo 'look_ads_scale' 'Turn rate while aiming' @('40', '65  default', '80', '100  as without aiming') 'Percent of the stick speed while aiming down the sights (with the fixed speed).'
Add-QuickCombo 'mouse_sens' 'Mouse speed' @('25  slow', '50  default', '75', '100', '150  fast') 'Mouse look of a player on keyboard and mouse.'
[void]$script:QuickPanel.Children.Add((New-Text 'View' 16 'SemiBold' '0,10,0,6'))
Add-QuickCombo 'fov_scale' 'Field of view' @('100  as the game sets it', '85', '70  default', '60', '50  widest') 'Smaller number, wider view in every part of the screen.'
Add-QuickCombo 'layout_three_players' 'Three players' @('1  player 1 across the top, players 2 and 3 below', '0  three quarters, the fourth one black') 'Choose it before the match starts.'
[void]$script:QuickPanel.Children.Add((New-Text 'Players, devices and pad buttons are on the next tab.' 12 'Normal' '0,14,0,0'))
Sync-QuickTab

# ---- buttons -----------------------------------------------------------------------------
$script:BtnOpenIni.add_Click({ if (Test-File $script:GameIni) { Start-Process notepad $script:GameIni } })
$script:BtnOpenLog.add_Click({ if (Test-File $script:LogPath) { Start-Process notepad $script:LogPath } })

# ---- once a second: game state, pads, external ini changes, log tail ---------------------
function Read-LogTail {
    if (-not (Test-File $script:LogPath)) { return 'no log yet' }
    $fs = New-Object System.IO.FileStream($script:LogPath, 'Open', 'Read', 'ReadWrite')
    try {
        $take = [Math]::Min($fs.Length, 262144)
        [void]$fs.Seek(-$take, 'End')
        $buf = New-Object byte[] $take
        [void]$fs.Read($buf, 0, $take)
    } finally { $fs.Dispose() }
    $lines = [System.Text.Encoding]::ASCII.GetString($buf) -split "`r?`n"
    if ($lines.Count -gt 1) { $lines = $lines[1..($lines.Count - 1)] } # the first one is cut
    if ($script:LogQuiet.IsChecked) { $lines = $lines | Where-Object { $_ -notmatch '\[view\] |QQTECH|\[p2\] body:' } }
    $f = $script:LogFilter.Text.Trim()
    if ($f) { $lines = $lines | Where-Object { $_.IndexOf($f, [StringComparison]::OrdinalIgnoreCase) -ge 0 } }
    return (@($lines | Where-Object { $_ }) | Select-Object -Last 200) -join "`r`n"
}
$timer = New-Object System.Windows.Threading.DispatcherTimer
$timer.Interval = [TimeSpan]::FromMilliseconds(1000)
$timer.add_Tick({
    try {
        $p = Get-Process t6zm -ErrorAction SilentlyContinue | Select-Object -First 1
        $script:GameState.Text = if ($p) { "game running (pid $($p.Id))" } else { 'game not running' }
        $script:GameState.Foreground = if ($p) { '#1a7f37' } else { '#9a6700' }
        Update-DeviceLists
        Sync-QuickTab
        $editing = $script:Grid -and $script:Grid.IsKeyboardFocusWithin
        if ((Test-File $script:GameIni) -and (Get-Item $script:GameIni).LastWriteTimeUtc -ne $script:IniStamp -and -not $editing) {
            Import-Values; Sync-PadTab
            if ($script:AfterReload) { & $script:AfterReload }
            $script:Status.Text = 'game-folder ini changed outside, values reloaded'
        }
        if ($script:LogLive.IsChecked) {
            $text = Read-LogTail
            if ($text -ne $script:LogBox.Text) { $script:LogBox.Text = $text; $script:LogBox.ScrollToEnd() }
        }
    } catch { $script:Status.Text = "refresh failed: $($_.Exception.Message)" }
})
$padTimer = New-Object System.Windows.Threading.DispatcherTimer
$padTimer.Interval = [TimeSpan]::FromMilliseconds(100)
$padTimer.add_Tick({ try { Update-PadState } catch { $script:Status.Text = "pad refresh failed: $($_.Exception.Message)" } })
$win.add_Closed({ $timer.Stop(); $padTimer.Stop() })

# ---- development part (not published) --------------------------------------------------
$devScript = Join-Path $PSScriptRoot 'panel-dev.ps1'
if (Test-Path -LiteralPath $devScript) {
    try { . $devScript } catch { $script:Status.Text = "panel-dev.ps1 failed: $($_.Exception.Message)" }
}

# ---- start -------------------------------------------------------------------------------
if (-not $script:GameDir) {
    $script:BannerText.Text = "The game folder was not found. Copy panel.ps1 and panel.bat into the game folder (the one with t6zm.exe), or start the panel with -GameDir ""<path of the game folder>""."
    $script:Banner.Visibility = 'Visible'; $script:Tabs.IsEnabled = $false
    $script:Status.Text = 'no game folder'
} elseif (-not (Test-File $script:GameIni)) {
    $script:BannerText.Text = "There is no bo2ss.ini in the game folder ($($script:GameDir)). Copy the mod's files (XINPUT1_3.dll and bo2ss.ini) there first."
    $script:Banner.Visibility = 'Visible'; $script:Tabs.IsEnabled = $false
    $script:Status.Text = 'no bo2ss.ini in the game folder'
}
$timer.Start()
$padTimer.Start()
if ($Tab -eq 'pads') { $script:Tabs.SelectedIndex = 1 }
[void]$win.ShowDialog()
