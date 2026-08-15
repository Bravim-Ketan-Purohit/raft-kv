/** @type {import('tailwindcss').Config} */
export default {
    content: ['./index.html', './src/**/*.{js,ts,jsx,tsx}'],
    theme: {
        extend: {
            colors: {
                leader: '#22c55e',
                follower: '#3b82f6',
                candidate: '#eab308',
                dead: '#6b7280',
            },
        },
    },
    plugins: [],
};
