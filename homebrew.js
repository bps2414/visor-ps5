async function main() {
    const CWD = window.workingDir;

    return {
        mainText: "Visor",
        secondaryText: 'Quest on the TV',
        onclick: async () => {
            return {
                path: CWD + '/visor.elf',
                cwd: CWD
            };
        }
    };
}
