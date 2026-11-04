"""The player's settings (docs/analysis/ingame-settings.md 3): the table that the in-game settings
overlay and the launcher share, written into launcher.json's "settings" by pipeline.py
(launcher_data). pipeline.py imports it; it reads pipeline.py's knob tables and look presets
through the running pipeline module."""
import pipeline as p

# The player's settings (docs/analysis/ingame-settings.md 3): the launcher's options and knobs that
# the in-game settings overlay (work/fm4-rex/src/fm4-settings-overlay.h) shows, by page and group,
# with when a change takes effect. live: the runtime reads the value again while running, so the
# overlay applies it at once (a cvar, a post pass param, or the window's fullscreen). restart: read
# once at start (a cached variable, a device, the swap chain or render targets) or bound to the
# shader pass; the overlay saves it and the launcher uses it at the next start. Rows of -Pad,
# -Keyboard and -Wheel take their cvar, default and range from those tables. The look's rows that
# name a param change a post pass param through shader_mod_set, live where the look the run
# started with allows it (PLAYER_LOOK_PARAMS). launcher.json carries the rows (rows).
PLAYER_SETTING_CLASSES=('live','restart')
PLAYER_SETTINGS=(
    # page, group, option, knob (None: the option itself), label, class
    ('controls','Pad','pad','rumble','Rumble strength','live'),
    ('controls','Pad','pad','trigger_rumble','Trigger rumble','live'),
    ('controls','Pad','pad','trigger_rumble_strength','Trigger rumble strength','live'),
    ('controls','Pad','pad','stick_deadzone','Stick deadzone','live'),
    ('controls','Pad','pad','stick_outer','Stick outer edge','live'),
    ('controls','Pad','pad','stick_curve','Stick curve','live'),
    ('controls','Pad','pad','anti_deadzone','Steering anti-deadzone','live'),
    ('controls','Pad','pad','trigger_deadzone','Trigger deadzone','live'),
    ('controls','Pad','pad','trigger_outer','Trigger outer edge','live'),
    # The SDL drivers start with the input system (sdl_input_driver.cpp, ApplyDriverHints).
    ('controls','Pad','pad','driver','Pad driver','restart'),
    ('controls','Keyboard','keyboard','smooth','Steering and pedal model','live'),
    ('controls','Keyboard','keyboard','steer_attack','Steering attack','live'),
    ('controls','Keyboard','keyboard','steer_release','Steering release','live'),
    ('controls','Keyboard','keyboard','throttle_attack','Throttle attack','live'),
    ('controls','Keyboard','keyboard','throttle_release','Throttle release','live'),
    ('controls','Keyboard','keyboard','brake_attack','Brake attack','live'),
    ('controls','Keyboard','keyboard','brake_release','Brake release','live'),
    ('controls','Keyboard','keyboard','precision','Precision steering','live'),
    ('controls','Keyboard','keyboard','mouse_steering','Mouse steering','live'),
    ('controls','Keyboard','keyboard','mouse_steer_sensitivity','Mouse steering sensitivity','live'),
    ('controls','Keyboard','keyboard','mouse_steer_centering','Mouse steering centring','live'),
    # The wheel's host takes its options once, as it starts (sdl_input_driver.cpp, StartWheel).
    ('controls','Racing wheel','wheel','on','Racing wheel','restart'),
    ('controls','Racing wheel','wheel','ffb_gain','Force feedback gain','restart'),
    ('controls','Racing wheel','wheel','ffb_cap','Force feedback cap','restart'),
    ('look','Look','look','preset','Look','restart'),
    ('look','Look','look','taa','Temporal anti-aliasing','live'),
    ('look','Look','look','sharpen','Sharpening','live'),
    ('look','Look','look','windshield','Windshield','live'),
    ('look','Look','look','ao','Ambient occlusion','live'),
    ('display','Screen','fullscreen',None,'Fullscreen','live'),
    ('display','Screen','display',None,'Screen','restart'),
    ('display','Screen','resolution_scale',None,'Render scale','restart'),
    ('display','Performance','upscale',None,'Upscaler','restart'),
    ('display','Performance','frame_rate',None,'Race frame rate','restart'),
    ('display','Performance','vsync',None,'VSync','restart'),
    ('display','Performance','quality',None,'Race quality','restart'),
)
PLAYER_KNOB_TABLES={'pad':p.PAD_KNOBS,'keyboard':p.KEYBOARD_KNOBS,'wheel':p.WHEEL_KNOBS}
# The look's live rows: the post pass param each changes and its range. taa and sharpen are live
# while TAA runs at the final point (aa=taa without taa_point), where the look's sharpening is
# TAA_SHARPEN; windshield while the post passes run; ao scales the occlusion of the look the run
# started with through GTAO_SCALE, so it is live up to that look's strength.
PLAYER_LOOK_PARAMS={'taa':('TAA',0.0,1.0),'sharpen':('TAA_SHARPEN',0.0,1.0),
                    'windshield':('WINDSHIELD',0.0,2.0),'ao':('GTAO_SCALE',0.0,1.0)}
# The names of the looks, as the launcher's Look page shows them.
PLAYER_LOOK_NAMES={p.DEFAULT_LOOK:f'{p.DEFAULT_LOOK} (default)','gt7':'GT7 style','fm2023':'FM2023 style'}
# The choices of the option rows: [the value the launcher stores, its name].
PLAYER_CHOICES={
    'fullscreen':[['0','Off'],['1','On']],
    'display':[['','Native'],['720p','1280x720 window'],['1080p','1920x1080 window'],
               ['1440p','2560x1440 window'],['4k','3840x2160 window']],
    'resolution_scale':[['0','Auto'],['-1',"Screen's own"],
                        *[[str(n),f'{n}x ({1280*n}x{720*n})'] for n in range(1,p.MAX_RESOLUTION_SCALE+1)]],
    'upscale':[['','Off'],['quality','Quality'],['balanced','Balanced'],['performance','Performance']],
    'frame_rate':[[str(0 if rate==60 else rate),f'{rate} fps'] for rate in (30,60,90,120,144,240)],
    'vsync':[['','Auto'],['on','On'],['off','Off']],
    'quality':[['low','Low'],['medium','Medium'],['','High'],['extrahigh','Extra High'],['ultra','Ultra']],
}

def rows():
    """The rows of PLAYER_SETTINGS for launcher.json: page, group, option, knob (empty for the
    option itself), label and class; the cvar or the param a live row sets; the launcher's
    default; and either choices ([value, name] pairs) or min, max and integer."""
    look_defaults=p.look_knobs(p.DEFAULT_LOOK)
    rows=[]
    for page,group,option,knob,label,klass in PLAYER_SETTINGS:
        if klass not in PLAYER_SETTING_CLASSES:
            raise ValueError(f'Unknown class {klass!r} of the player setting {option} {knob}.')
        row={'page':page,'group':group,'option':option,'knob':knob or '','label':label,'class':klass}
        if option in PLAYER_KNOB_TABLES:
            spec=PLAYER_KNOB_TABLES[option][knob]
            row.update(cvar=spec['cvar'],default=spec['default'])
            if 'choices' in spec:
                switch=spec['choices']==['0','1']
                row['choices']=[[value,{'0':'Off','1':'On'}[value] if switch else value]
                                for value in spec['choices']]
            else:
                row.update(min=spec['min'],max=spec['max'],integer=bool(spec.get('integer')))
        elif option=='look' and knob=='preset':
            row.update(default=p.DEFAULT_LOOK,
                       choices=[[name,PLAYER_LOOK_NAMES.get(name,name)] for name in p.LOOK_PRESETS])
        elif option=='look':
            param,low,high=PLAYER_LOOK_PARAMS[knob]
            row.update(param=param,default=look_defaults.get(knob,'0'))
            if knob=='taa':
                row['choices']=[['0','Off'],['1','On']]
            else:
                row.update(min=low,max=high,integer=False)
        else:
            choices=PLAYER_CHOICES[option]
            # The launcher's own: empty text, or 0 for the numbers and fullscreen.
            row.update(default='' if option in ('display','upscale','vsync','quality') else '0',
                       choices=choices)
        rows.append(row)
    return rows
