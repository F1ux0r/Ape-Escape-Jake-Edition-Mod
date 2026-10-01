p = "src/mods/ape_power_up_gadgets_plugin.c"
s = open(p).read()


def sub(old, new):
    global s
    assert s.count(old) == 1, old[:70]
    s = s.replace(old, new)


# The fold code needs the grip section's matrix constant; insert it before
# the handler hook, which now poses the matrix before capturing it.
marker = "static void ape_flyer_handler_entry(struct CPUState* cpu, uint32_t address) {\n    ApeDrawFrame* f"
assert s.count(marker) == 1
s = s.replace(marker, open(r"D:/Ape Escape Recomp/modding/re/fold_code.c").read() + "\n" + marker)

sub("""    ApeAffine m;
    int i, j;
    (void)cpu;
    (void)address;
    ape_read_affine(APE_FLYER_MODEL_MATRIX, &m);
    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j)
            f->flyer_r[i][j] = m.r[i][j];""", """    ApeAffine m;
    int i, j;
    (void)cpu;
    (void)address;
    if (g_flyer_power.on)
        ape_pose_flyer_matrix();   /* carried: pointing down */
    ape_read_affine(APE_FLYER_MODEL_MATRIX, &m);
    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j)
            f->flyer_r[i][j] = m.r[i][j];""")

# blades follow the unfold every VBlank while powered
sub("""    (void)ape_update_power_toggle(&g_flyer_power, &slot);
    ape_apply_flyer_model(g_flyer_power.on);""", """    (void)ape_update_power_toggle(&g_flyer_power, &slot);
    ape_apply_flyer_model(g_flyer_power.on);
    if (g_flyer_power.on) {
        ape_update_flyer_unfold();
        ape_shape_blades_folded(g_flyer_unfold);
    }""")
sub("""static void ape_draw_flyer_grip(void);
static void ape_flyer_handler_entry(struct CPUState* cpu, uint32_t address);""", """static void ape_draw_flyer_grip(void);
static void ape_flyer_handler_entry(struct CPUState* cpu, uint32_t address);
static void ape_update_flyer_unfold(void);
static void ape_shape_blades_folded(float u);
static float g_flyer_unfold;""")
# the forward declaration above replaces the initialised definition
s = s.replace("static float g_flyer_unfold = 1.0f;\n", "")
open(p, "w", newline="").write(s)
print("applied")
